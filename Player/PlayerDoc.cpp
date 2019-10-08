
// PlayerDoc.cpp : implementation of the CPlayerDoc class
//

#include "stdafx.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "Player.h"
#endif

#include "PlayerDoc.h"
#include "AudioPlayerImpl.h"
#include "AudioPlayerWasapi.h"
#include "HandleFilesSequence.h"
#include "AudioPitchDecorator.h"

#include "DialogOpenURL.h"

#include "YouTuber.h"

#include <propkey.h>
#include <memory>

#include <boost/icl/interval_map.hpp>
#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <fstream>

#include <string>
#include <cctype>

#include <VersionHelpers.h>

#include <sensapi.h>
#pragma comment(lib, "Sensapi")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


namespace {

const std::pair<int, int> videoSpeeds[] 
{
    { 1, 2 },
    { 5, 8 },
    { 4, 5 },
    { 1, 1 },
    { 5, 4 },
    { 8, 5 },
    { 2, 1 },
    { 5, 4 }, //nightcore
};


bool IsDeletingItemFromMruList()
{
    auto msg = AfxGetCurrentMessage();
    return msg && msg->message == WM_COMMAND
        && msg->wParam >= ID_FILE_MRU_FILE1
        && msg->wParam < ID_FILE_MRU_FILE1 + _AFX_MRU_MAX_COUNT
        && GetAsyncKeyState(VK_SHIFT) < 0
        && GetAsyncKeyState(VK_CONTROL) < 0;
}

CString GetUrlFromUrlFile(LPCTSTR lpszPathName)
{
    CString url;
    auto result = GetPrivateProfileString(
        _T("InternetShortcut"),
        _T("URL"),
        nullptr,
        url.GetBuffer(4095),
        4096,
        lpszPathName);
    if (!result)
        return {};
    url.ReleaseBuffer();
    return url;
}

std::unique_ptr<IAudioPlayer> GetAudioPlayer()
{
    if (IsWindowsVistaOrGreater())
        return std::make_unique<AudioPlayerWasapi>();
    return std::make_unique<AudioPlayerImpl>();
}       

} // namespace


class CPlayerDoc::SubtitlesMap : public boost::icl::interval_map<double, std::string>
{};

// CPlayerDoc

IMPLEMENT_DYNCREATE(CPlayerDoc, CDocument)

BEGIN_MESSAGE_MAP(CPlayerDoc, CDocument)
    ON_COMMAND_RANGE(ID_TRACK1, ID_TRACK4, OnAudioTrack)
    ON_UPDATE_COMMAND_UI_RANGE(ID_TRACK1, ID_TRACK4, OnUpdateAudioTrack)
    ON_COMMAND_RANGE(ID_VIDEO_SPEED1, ID_NIGHTCORE, OnVideoSpeed)
    ON_UPDATE_COMMAND_UI_RANGE(ID_VIDEO_SPEED1, ID_NIGHTCORE, OnUpdateVideoSpeed)
    ON_COMMAND(ID_AUTOPLAY, &CPlayerDoc::OnAutoplay)
    ON_UPDATE_COMMAND_UI(ID_AUTOPLAY, &CPlayerDoc::OnUpdateAutoplay)
    ON_COMMAND(ID_LOOPING, &CPlayerDoc::OnLooping)
    ON_UPDATE_COMMAND_UI(ID_LOOPING, &CPlayerDoc::OnUpdateLooping)
    ON_COMMAND(ID_FILE_SAVE_COPY_AS, &CPlayerDoc::OnFileSaveCopyAs)
END_MESSAGE_MAP()


// CPlayerDoc construction/destruction

CPlayerDoc::CPlayerDoc()
    : m_frameDecoder(
        GetFrameDecoder(
            std::make_unique<AudioPitchDecorator>(GetAudioPlayer(),
            std::bind(&CPlayerDoc::getVideoSpeed, this))))
    , m_unicodeSubtitles(false)
    , m_onEndOfStream(false)
    , m_autoPlay(false)
    , m_looping(false)
    , m_nightcore(false)
{
    m_frameDecoder->setDecoderListener(this);
}

CPlayerDoc::~CPlayerDoc()
{
    ASSERT(framePositionChanged.empty());
    ASSERT(startTimeUpdated.empty());
    ASSERT(totalTimeUpdated.empty());
    ASSERT(currentTimeUpdated.empty());

    ASSERT(rangeStartTimeChanged.empty());
    ASSERT(rangeEndTimeChanged.empty());
}

BOOL CPlayerDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
        return FALSE;

    // (SDI documents will reuse this document)

    if (AfxGetApp()->m_pMainWnd) // false if the document is being initialized for the first time
    {
        CDialogOpenURL dlg;
        if (dlg.DoModal() == IDOK && !dlg.m_URL.IsEmpty())
        {
            reset();
            openTopLevelUrl(dlg.m_URL, dlg.m_bParse);
        }
    }

    return TRUE;
}

bool CPlayerDoc::openTopLevelUrl(const CString& topLevelUrl, bool force, const CString& pathName)
{
    std::string url(CT2A(topLevelUrl, CP_UTF8));

    auto playList = ParsePlaylist(url, force);

    if (!playList.empty())
    {
        m_playList = { playList.begin(), playList.end() };

        if (openUrlFromList())
        {
            m_reopenFunc = [this, playList, pathName] {
                UpdateAllViews(nullptr, UPDATE_HINT_CLOSING);
                m_playList = { playList.begin(), playList.end() };
                if (openUrlFromList() && !pathName.IsEmpty())
                    SetPathName(pathName, FALSE);
            };
            return true;
        }
    }
    else if (openUrl(url))
    {
        m_playList.clear();
        m_reopenFunc = [this, url, pathName] {
            UpdateAllViews(nullptr, UPDATE_HINT_CLOSING);
            if (openUrl(url) && !pathName.IsEmpty())
                SetPathName(pathName, FALSE);
        };
        return true;
    }

    return false;
}

bool CPlayerDoc::openUrl(const std::string& originalUrl)
{
    const auto url = getYoutubeUrl(originalUrl);
    if (!url.empty() && m_frameDecoder->openUrl(url))
    {
        m_frameDecoder->play(true);
        m_url = url;
        m_subtitles.reset();
        m_nightcore = false;
        auto transcripts = getYoutubeTranscripts(originalUrl);
        if (!transcripts.empty())
        {
            // TODO refactor
            m_unicodeSubtitles = true;
            auto map(std::make_unique<SubtitlesMap>());
            for (const auto& v : transcripts)
            {
                map->add({
                    boost::icl::interval<double>::closed(v.start, v.start + v.duration),
                    boost::algorithm::trim_copy(v.text) + '\n' });
            }
            if (!map->empty())
            {
                m_subtitles = std::move(map);
            }
        }
        m_frameDecoder->pauseResume();
        onPauseResume(false);
        return true;
    }

    return false;
}

bool CPlayerDoc::openUrlFromList()
{
    bool networkCkecked = false;

    while (!m_playList.empty())
    {
        auto buffer = m_playList.front();
        m_playList.pop_front();
        if (openUrl(buffer))
            return true;
        else if (!networkCkecked)
        {
            networkCkecked = true;
            DWORD flags = NETWORK_ALIVE_INTERNET;
            if (!IsNetworkAlive(&flags))
            {
                m_playList.push_front(buffer);
                return false;
            }
        }
    }
    return false;
}

void CPlayerDoc::reset()
{
    m_frameDecoder->close();
    m_subtitles.reset();
    m_reopenFunc = nullptr;

    m_url.clear();

    m_nightcore = false;

    UpdateAllViews(nullptr, UPDATE_HINT_CLOSING);
}

// CPlayerDoc serialization

void CPlayerDoc::Serialize(CArchive& ar)
{
    if (ar.IsStoring())
    {
        // TODO: add storing code here
    }
    else
    {
        // TODO: add loading code here
    }
}

#ifdef SHARED_HANDLERS

// Support for thumbnails
void CPlayerDoc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
    // Modify this code to draw the document's data
    dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

    CString strText = _T("TODO: implement thumbnail drawing here");
    LOGFONT lf;

    CFont* pDefaultGUIFont = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
    pDefaultGUIFont->GetLogFont(&lf);
    lf.lfHeight = 36;

    CFont fontDraw;
    fontDraw.CreateFontIndirect(&lf);

    CFont* pOldFont = dc.SelectObject(&fontDraw);
    dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
    dc.SelectObject(pOldFont);
}

// Support for Search Handlers
void CPlayerDoc::InitializeSearchContent()
{
    CString strSearchContent;
    // Set search contents from document's data. 
    // The content parts should be separated by ";"

    // For example:  strSearchContent = _T("point;rectangle;circle;ole object;");
    SetSearchContent(strSearchContent);
}

void CPlayerDoc::SetSearchContent(const CString& value)
{
    if (value.IsEmpty())
    {
        RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
    }
    else
    {
        CMFCFilterChunkValueImpl *pChunk = NULL;
        ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
        if (pChunk != NULL)
        {
            pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
            SetChunkValue(pChunk);
        }
    }
}

#endif // SHARED_HANDLERS

// CPlayerDoc diagnostics

#ifdef _DEBUG
void CPlayerDoc::AssertValid() const
{
    CDocument::AssertValid();
}

void CPlayerDoc::Dump(CDumpContext& dc) const
{
    CDocument::Dump(dc);
}
#endif //_DEBUG


// CPlayerDoc commands


BOOL CPlayerDoc::OnOpenDocument(LPCTSTR lpszPathName)
{
    if (IsDeletingItemFromMruList())
        return false;

    return openDocument(lpszPathName);
}

BOOL CPlayerDoc::OnSaveDocument(LPCTSTR lpszPathName)
{
    const bool isLocalFile = m_url.empty();
    CString source(isLocalFile
        ? m_strPathName
        : CString(m_url.data(), m_url.length()));

    if (source.IsEmpty())
        return false;

    if (isLocalFile && isFullFrameRange())
    {
        return CopyFile(source, lpszPathName, TRUE);
    }

    CString strFile;
    CString strParams;
    if (isFullFrameRange())
    {
        TCHAR pszPath[MAX_PATH] = { 0 };
        GetModuleFileName(NULL, pszPath, ARRAYSIZE(pszPath));
        PathRemoveFileSpec(pszPath);
        PathAppend(pszPath, _T("HttpDownload.exe"));
        strFile = pszPath;
        strParams = source + _T(" \"") + lpszPathName + _T('"');
    }
    else
    {
        strFile = _T("ffmpeg.exe");
        strParams.Format(
            _T("-ss %.3f -t %.3f -i \"%s\" \"%s\""),
            m_rangeStartTime,
            m_rangeEndTime - m_rangeStartTime,
            source,
            lpszPathName);
    }
    const  auto result = ShellExecute(NULL, NULL, strFile, strParams, NULL, SW_MINIMIZE);
    return int(result) > 32;
}


bool CPlayerDoc::openDocument(LPCTSTR lpszPathName)
{
    reset();

    const auto extension = PathFindExtension(lpszPathName);
    if (!_tcsicmp(extension, _T(".lst")))
    {
        std::ifstream s(lpszPathName);
        if (!s)
            return false;
        m_playList.clear();
        std::string buffer;
        while (std::getline(s, buffer))
        {
            m_playList.push_back(buffer);
        }

        if (!openUrlFromList())
            return false;
    }
    else if (!_tcsicmp(extension, _T(".url")))
    {
        CString url = GetUrlFromUrlFile(lpszPathName);
        return !url.IsEmpty() && openTopLevelUrl(url, false, lpszPathName); // sets m_reopenFunc
    }
    else
    {
        if (extension[0] == _T('\0') || !_tcsicmp(extension, _T(".html"))) // https://community.spiceworks.com/topic/1968971-opening-web-links-downloading-1-item-to-zcrksihu
        {
            auto playList = ParsePlaylistFile(lpszPathName);
            if (!playList.empty())
            {
                m_playList = { playList.begin(), playList.end() };

                if (openUrlFromList())
                {
                    m_reopenFunc = [this, playList] {
                        UpdateAllViews(nullptr, UPDATE_HINT_CLOSING);
                        m_playList = { playList.begin(), playList.end() };
                        openUrlFromList();
                    };
                    return true;
                }
                return false;
            }
        }

        if (!m_frameDecoder->openFile(lpszPathName))
            return false;
        m_playList.clear();
        if (!OpenSubRipFile(lpszPathName))
            OpenSubStationAlphaFile(lpszPathName);
        m_frameDecoder->play();
        onPauseResume(false);
    }

    m_reopenFunc = [this, path = CString(lpszPathName)] {
        if (openDocument(path))
            SetPathName(path, FALSE);
    };
    return true;
}

void CPlayerDoc::OnIdle()
{
    __super::OnIdle();

    if (m_onEndOfStream)
    {
        m_onEndOfStream = false;
        MoveToNextFile();
    }
}

void CPlayerDoc::OnFileSaveCopyAs()
{
    if (!DoSave(NULL, FALSE))
        TRACE(traceAppMsg, 0, "Warning: File save-as failed.\n");
}

void CPlayerDoc::MoveToNextFile()
{
    auto saveReopenFunc = m_reopenFunc;

    if (openUrlFromList() || m_autoPlay && HandleFilesSequence(
        GetPathName(), 
        m_looping,
        [this](const CString& path) 
        {
            if (openDocument(path))
            {
                SetPathName(path, FALSE);
                return true;
            }
            return false;
        }))
    {
        m_reopenFunc = saveReopenFunc;
        return;
    }

    if (m_playList.empty() && m_looping && m_reopenFunc)
    {
        // m_reopenFunc can be reset during invocation
        auto tempReopenFunc = m_reopenFunc;
        tempReopenFunc();
    }
}

void CPlayerDoc::OnCloseDocument()
{
    m_frameDecoder->close();
    m_subtitles.reset();
    CDocument::OnCloseDocument();
}


void CPlayerDoc::changedFramePosition(long long start, long long frame, long long total)
{
    framePositionChanged(frame - start, total - start);
    const double currentTime = m_frameDecoder->getDurationSecs(frame);
    m_currentTime = currentTime;
    currentTimeUpdated(currentTime);

    if (m_looping && !m_autoPlay && !isFullFrameRange() && m_currentTime >= m_rangeEndTime)
    {
        const double percent = (m_rangeStartTime - m_startTime) / (m_endTime - m_startTime);
        m_frameDecoder->seekByPercent(percent);
    }
}

void CPlayerDoc::fileLoaded(long long start, long long total)
{
    const double startTime = m_frameDecoder->getDurationSecs(start);
    m_startTime = startTime;
    startTimeUpdated(startTime);

    const double endTime = m_frameDecoder->getDurationSecs(total);
    m_endTime = endTime;
    totalTimeUpdated(endTime);

    setRangeStartTime(startTime);
    setRangeEndTime(endTime);

    if (CWnd* pMainWnd = AfxGetApp()->GetMainWnd())
        pMainWnd->PostMessage(WM_KICKIDLE); // trigger idle update
}

void CPlayerDoc::onEndOfStream()
{
    if (m_looping && !m_autoPlay && !isFullFrameRange())
    {
        const double percent = (m_rangeStartTime - m_startTime) / (m_endTime - m_startTime);
        if (m_frameDecoder->seekByPercent(percent))
            return;
    }

    m_onEndOfStream = true;

    if (CWnd* pMainWnd = AfxGetApp()->GetMainWnd())
        pMainWnd->PostMessage(WM_KICKIDLE); // trigger idle update
}

bool CPlayerDoc::pauseResume()
{
    if (m_frameDecoder->pauseResume())
    {
        onPauseResume(m_frameDecoder->isPaused());
        return true;
    }
    return false;
}

bool CPlayerDoc::nextFrame()
{
    return m_frameDecoder->nextFrame();
}

bool CPlayerDoc::seekByPercent(double percent)
{
    return m_frameDecoder->seekByPercent(percent);
}

void CPlayerDoc::setVolume(double volume)
{
    m_frameDecoder->setVolume(volume);
}

bool CPlayerDoc::isPlaying() const
{
    return m_frameDecoder->isPlaying();
}

bool CPlayerDoc::isPaused() const
{
    return m_frameDecoder->isPaused();
}

double CPlayerDoc::soundVolume() const
{
    return m_frameDecoder->volume();
}

bool CPlayerDoc::OpenSubRipFile(LPCTSTR lpszVideoPathName)
{
    m_subtitles.reset();

    CString subRipPathName(lpszVideoPathName);
    PathRemoveExtension(subRipPathName.GetBuffer());
    subRipPathName.ReleaseBuffer();
    subRipPathName += _T(".srt");

    std::ifstream s(subRipPathName);
    if (!s)
        return false;

    auto map(std::make_unique<SubtitlesMap>());

    std::string buffer;
    bool first = true;
    while (std::getline(s, buffer))
    {
        if (first)
        {
            m_unicodeSubtitles = buffer.length() > 2
                && buffer[0] == char(0xEF) && buffer[1] == char(0xBB) && buffer[2] == char(0xBF);
            first = false;
        }

        if (std::find_if_not(buffer.begin(), buffer.end(), static_cast<int(*)(int)>(std::isspace))
            == buffer.end())
        {
            continue;
        }

        if (!std::getline(s, buffer))
            break;

        int startHr, startMin, startSec, startMsec;
        int endHr, endMin, endSec, endMsec;

        if (sscanf(
            buffer.c_str(),
            "%d:%d:%d,%d --> %d:%d:%d,%d",
            &startHr, &startMin, &startSec, &startMsec,
            &endHr, &endMin, &endSec, &endMsec) != 8)
        {
            break;
        }

        const double start = startHr * 3600 + startMin * 60 + startSec + startMsec / 1000.;
        const double end = endHr * 3600 + endMin * 60 + endSec + endMsec / 1000.;

        std::string subtitle;
        while (std::getline(s, buffer) && !buffer.empty())
        {
            subtitle += buffer;
            subtitle += '\n'; // The last '\n' is for aggregating overlapped subtitles (if any)
        }

        if (!subtitle.empty())
        {
            map->add({ boost::icl::interval<double>::closed(start, end), subtitle });
        }
    }

    if (!map->empty())
    {
        m_subtitles = std::move(map);
    }

    return true;
}

bool CPlayerDoc::OpenSubStationAlphaFile(LPCTSTR lpszVideoPathName)
{
    m_subtitles.reset();

    for (auto ext : { _T(".ass"), _T(".ssa") })
    {
        CString subRipPathName(lpszVideoPathName);
        PathRemoveExtension(subRipPathName.GetBuffer());
        subRipPathName.ReleaseBuffer();
        subRipPathName += ext;

        std::ifstream s(subRipPathName);
        if (!s)
            continue;

        auto map(std::make_unique<SubtitlesMap>());

        std::string buffer;
        bool first = true;
        while (std::getline(s, buffer))
        {
            if (first)
            {
                m_unicodeSubtitles = buffer.length() > 2
                    && buffer[0] == char(0xEF) && buffer[1] == char(0xBB) && buffer[2] == char(0xBF);
                first = false;
            }

            std::istringstream ss(buffer);

            std::getline(ss, buffer, ':');

            if (buffer != "Dialogue")
                continue;

            double start, end;
            bool skip = false;
            for (int i = 0; i < 9; ++i) // TODO indices from Format?
            {
                std::getline(ss, buffer, ',');
                if (i == 1 || i == 2)
                {
                    int hr, min, sec;
                    char msecString[10]{};
                    if (sscanf(
                        buffer.c_str(),
                        "%d:%d:%d.%9s",
                        &hr, &min, &sec, msecString) != 4)
                    {
                        return true;
                    }

                    double msec = 0;
                    if (const auto msecStringLen = strlen(msecString))
                    {
                        msec = atoi(msecString) / pow(10, msecStringLen);
                    }

                    ((i == 1) ? start : end)
                        = hr * 3600 + min * 60 + sec + msec;
                }
                else if (i == 3 && buffer == "OP_kar")
                {
                    skip = true;
                    break;
                }
            }

            if (skip)
                continue;

            std::string subtitle;
            if (!std::getline(ss, subtitle, '\\'))
                continue;
            while (std::getline(ss, buffer, '\\'))
            {
                subtitle +=
                    (buffer[0] == 'N' || buffer[0] == 'n') ? '\n' + buffer.substr(1) : '\\' + buffer;
            }

            if (!subtitle.empty())
            {
                subtitle += '\n'; // The last '\n' is for aggregating overlapped subtitles (if any)
                map->add({ boost::icl::interval<double>::closed(start, end), subtitle });
            }
        }

        if (!map->empty())
        {
            m_subtitles = std::move(map);
        }

        return true;
    }
    return false;
}

std::string CPlayerDoc::getSubtitle() const
{
    std::string result;
    if (m_subtitles)
    {
        auto it = m_subtitles->find(m_currentTime); 
        if (it != m_subtitles->end())
        {
            result = it->second;
        }
    }
    return result;
}

void CPlayerDoc::setRangeStartTime(double time)
{
    m_rangeStartTime = time;
    rangeStartTimeChanged(time - m_startTime, m_endTime - m_startTime);
}

void CPlayerDoc::setRangeEndTime(double time)
{
    m_rangeEndTime = time;
    rangeEndTimeChanged(time - m_startTime, m_endTime - m_startTime);
}

bool CPlayerDoc::isFullFrameRange() const
{
    return m_startTime == m_rangeStartTime && m_endTime == m_rangeEndTime;
}

void CPlayerDoc::OnAsyncUrl(const CString& url)
{
    if (!url.IsEmpty() && m_strPathName.IsEmpty() && m_url.empty())
    {
        openTopLevelUrl(url, false);
    }
}

void CPlayerDoc::OnDropFiles(HDROP hDropInfo)
{
    TCHAR lpszFileName[MAX_PATH];
    if (DragQueryFile(hDropInfo, 0, lpszFileName, MAX_PATH)
        && openDocument(lpszFileName))
    {
        SetPathName(lpszFileName, TRUE);
    }
}

void CPlayerDoc::OnAudioTrack(UINT id)
{
    m_frameDecoder->setAudioTrack(id - ID_TRACK1);
}

void CPlayerDoc::OnUpdateAudioTrack(CCmdUI* pCmdUI)
{
    const int idx = pCmdUI->m_nID - ID_TRACK1;
    pCmdUI->Enable(idx < m_frameDecoder->getNumAudioTracks());
    pCmdUI->SetCheck(idx == m_frameDecoder->getAudioTrack());
}


void CPlayerDoc::OnAutoplay()
{
    m_autoPlay = !m_autoPlay;
}


void CPlayerDoc::OnUpdateAutoplay(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_autoPlay);
}

void CPlayerDoc::OnLooping()
{
    m_looping = !m_looping;
}


void CPlayerDoc::OnUpdateLooping(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_looping);
}

void CPlayerDoc::OnVideoSpeed(UINT id)
{
    const int idx = id - ID_VIDEO_SPEED1;
    if (idx >= 0 && idx < sizeof(videoSpeeds) / sizeof(videoSpeeds[0]))
    {
        m_frameDecoder->setSpeedRational(videoSpeeds[idx]);
        m_nightcore = (id == ID_NIGHTCORE);
    }
}

void CPlayerDoc::OnUpdateVideoSpeed(CCmdUI* pCmdUI)
{
    const int idx = pCmdUI->m_nID - ID_VIDEO_SPEED1;
    if (idx >= 0 && idx < sizeof(videoSpeeds) / sizeof(videoSpeeds[0]))
    {
        pCmdUI->Enable(m_frameDecoder->isPlaying());
        pCmdUI->SetCheck((pCmdUI->m_nID == ID_NIGHTCORE) ? m_nightcore 
            : !m_nightcore && m_frameDecoder->getSpeedRational() == videoSpeeds[idx]);
    }
}

float CPlayerDoc::getVideoSpeed() const
{
    if (m_nightcore)
        return 1.f;
    const auto speedRational = m_frameDecoder->getSpeedRational();
    return static_cast<float>(speedRational.second) / speedRational.first;
}
