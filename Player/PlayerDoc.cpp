
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

#include "DialogOpenURL.h"

#include "YouTuber.h"

#include <propkey.h>
#include <memory>

#include <boost/icl/interval_map.hpp>

#include <algorithm>
#include <fstream>

#include <string>

#include <VersionHelpers.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


class CPlayerDoc::SubtitlesMap : public boost::icl::interval_map<double, std::string>
{};

// CPlayerDoc

IMPLEMENT_DYNCREATE(CPlayerDoc, CDocument)

BEGIN_MESSAGE_MAP(CPlayerDoc, CDocument)
    ON_COMMAND_RANGE(ID_TRACK1, ID_TRACK4, OnAudioTrack)
    ON_UPDATE_COMMAND_UI_RANGE(ID_TRACK1, ID_TRACK4, OnUpdateAudioTrack)
    ON_COMMAND(ID_AUTOPLAY, &CPlayerDoc::OnAutoplay)
    ON_UPDATE_COMMAND_UI(ID_AUTOPLAY, &CPlayerDoc::OnUpdateAutoplay)
    ON_COMMAND(ID_LOOPING, &CPlayerDoc::OnLooping)
    ON_UPDATE_COMMAND_UI(ID_LOOPING, &CPlayerDoc::OnUpdateLooping)
END_MESSAGE_MAP()


// CPlayerDoc construction/destruction

CPlayerDoc::CPlayerDoc()
    : m_frameDecoder(
    IsWindowsVistaOrGreater()
    ? GetFrameDecoder(std::make_unique<AudioPlayerWasapi>())
    : GetFrameDecoder(std::make_unique<AudioPlayerImpl>()))
    , m_unicodeSubtitles(false)
    , m_onEndOfStream(false)
    , m_autoPlay(false)
    , m_looping(false)
{
    m_frameDecoder->setDecoderListener(this);
}

CPlayerDoc::~CPlayerDoc()
{
    ASSERT(framePositionChanged.empty());
    ASSERT(totalTimeUpdated.empty());
    ASSERT(currentTimeUpdated.empty());
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
            m_frameDecoder->close();
            UpdateAllViews(nullptr, UPDATE_HINT_CLOSING, nullptr);
            std::string url(dlg.m_URL.GetString(), dlg.m_URL.GetString() + dlg.m_URL.GetLength());
            openUrl(url);
            m_reopenFunc = [this, url] {
                UpdateAllViews(nullptr, UPDATE_HINT_CLOSING, nullptr);
                openUrl(url);
            };
        }
    }

    return TRUE;
}

bool CPlayerDoc::openUrl(std::string url)
{
    url = getYoutubeUrl(url);
    if (m_frameDecoder->openUrl(url))
    {
        m_frameDecoder->play();
        return true;
    }

    return false;
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
    m_frameDecoder->close();
    UpdateAllViews(nullptr, UPDATE_HINT_CLOSING, nullptr);

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
        while (!m_playList.empty())
        {
            buffer = m_playList.front();
            m_playList.pop_front();
            if (openUrl(buffer))
                break;
        }
        if (m_playList.empty())
            return false;
    }
    else
    {
        if (!m_frameDecoder->openFile(lpszPathName))
            return false;
        if (!OpenSubRipFile(lpszPathName))
            OpenSubStationAlphaFile(lpszPathName);
        m_frameDecoder->play();
    }

    m_reopenFunc = [this, path = CString(lpszPathName)] {
        if (OnOpenDocument(path))
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

void CPlayerDoc::MoveToNextFile()
{
    if (!m_playList.empty())
    {
        do
        {
            auto buffer = m_playList.front();
            m_playList.pop_front();
            if (openUrl(buffer))
                return;
        } while (!m_playList.empty());
    }
    if (m_autoPlay)
    {
        const CString pathName = GetPathName();
        const auto extension = PathFindExtension(pathName);
        const auto fileName = PathFindFileName(pathName);
        if (!extension || !fileName)
            return;
        const CString directory(pathName, fileName - pathName);
        const CString pattern((directory + _T('*')) + extension);

        WIN32_FIND_DATA ffd{};
        const auto hFind = FindFirstFile(pattern, &ffd);

        if (INVALID_HANDLE_VALUE == hFind)
        {
            return;
        }

        std::vector<CString> files;
        const auto extensionLength = pathName.GetLength() - (extension - pathName);
        const CString justFileName(fileName, extension - fileName);

        do
        {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                const auto length = _tcslen(ffd.cFileName);
                if (length > extensionLength && ffd.cFileName[length - extensionLength] == _T('.'))
                {
                    ffd.cFileName[length - extensionLength] = 0;
                    if (_tcsicmp(justFileName, ffd.cFileName) < 0)
                    {
                        files.emplace_back(ffd.cFileName, length - extensionLength);
                    }
                }
            }
        } while (FindNextFile(hFind, &ffd));

        FindClose(hFind);

        auto comp = [](const CString& left, const CString& right) {
            return left.CompareNoCase(right) > 0;
        };
        std::make_heap(files.begin(), files.end(), comp);

        while (!files.empty())
        {
            const CString path = directory + files.front() + extension;
            if (OnOpenDocument(path))
            {
                SetPathName(path, FALSE);
                return;
            }
            std::pop_heap(files.begin(), files.end(), comp);
            files.pop_back();
        }
    }

    if (m_looping && m_reopenFunc)
        m_reopenFunc();
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
    totalTimeUpdated(m_frameDecoder->getDurationSecs(total));
    currentTimeUpdated(currentTime);
}

void CPlayerDoc::onEndOfStream()
{
    m_onEndOfStream = true;

    if (CWnd* pMainWnd = AfxGetApp()->GetMainWnd())
        pMainWnd->PostMessage(WM_KICKIDLE); // trigger idle update
}

bool CPlayerDoc::pauseResume()
{
    return m_frameDecoder->pauseResume();
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
            map->add(std::make_pair(boost::icl::interval<double>::closed(start, end), subtitle));
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

    CString subRipPathName(lpszVideoPathName);
    PathRemoveExtension(subRipPathName.GetBuffer());
    subRipPathName.ReleaseBuffer();
    subRipPathName += _T(".ass");

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
                char msecString[10] {};
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

                ((i == 1)? start : end)
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
                (buffer[0] == 'N' || buffer[0] == 'n')? '\n' + buffer.substr(1) : '\\' + buffer;
        }

        if (!subtitle.empty())
        {
            subtitle += '\n'; // The last '\n' is for aggregating overlapped subtitles (if any)
            map->add(std::make_pair(boost::icl::interval<double>::closed(start, end), subtitle));
        }
    }

    if (!map->empty())
    {
        m_subtitles = std::move(map);
    }

    return true;
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
