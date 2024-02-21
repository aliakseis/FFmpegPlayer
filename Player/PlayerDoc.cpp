
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
#include "OpenSubtitlesFile.h"

#include "DialogOpenURL.h"

#include "YouTuber.h"

#include "ImageUpscale.h"

#include "ByteStreamBuffer.h"

#include <propkey.h>
#include <memory>

#include <boost/icl/interval_map.hpp>
#include <boost/algorithm/string.hpp>

// vcpkg install dtl
#include <dtl/dtl.hpp>

#include <algorithm>
#include <fstream>

#include <string>
#include <cctype>

#include <atomic>
#include <filesystem>
#include <regex>

#include <VersionHelpers.h>

#include <sensapi.h>
#pragma comment(lib, "Sensapi")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


namespace {

void SetForegroundWindowInternal(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd) || ::SetForegroundWindow(hWnd))
        return;

    //to unlock SetForegroundWindow we need to imitate pressing [Alt] key
    bool bPressed = false;
    if ((::GetAsyncKeyState(VK_MENU) & 0x8000) == 0)
    {
        bPressed = true;
        ::keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | 0, 0);
    }

    ::SetForegroundWindow(hWnd);

    if (bPressed)
    {
        ::keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    }
}

const RationalNumber videoSpeeds[]
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


bool IsCalledFromMruList()
{
    auto msg = AfxGetCurrentMessage();
    return msg && msg->message == WM_COMMAND
        && msg->wParam >= ID_FILE_MRU_FILE1
        && msg->wParam < ID_FILE_MRU_FILE1 + _AFX_MRU_MAX_COUNT;
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

bool PumpMessages()
{
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
    {
        if (!AfxGetApp()->PumpMessage())
        {
            ::PostQuitMessage(0);
            return false;
        }
    }
    // let MFC do its idle processing
    LONG lIdle = 0;
    while (AfxGetApp()->OnIdle(lIdle++))
        ;

    return true;
}

void CopyTextToClipboard(const CString& strText)
{
#ifdef _UNICODE
    enum { AFX_TCF_TEXT = CF_UNICODETEXT };
#else
    enum { AFX_TCF_TEXT = CF_TEXT };
#endif

    if (!strText.IsEmpty() && AfxGetMainWnd()->OpenClipboard())
    {
        EmptyClipboard();

        const auto textSize = (strText.GetLength() + 1) * sizeof(TCHAR);
        if (HGLOBAL hClipbuffer = ::GlobalAlloc(GMEM_MOVEABLE, textSize))
        {
            if (LPTSTR lpszBuffer = (LPTSTR)GlobalLock(hClipbuffer))
            {
                memcpy(lpszBuffer, (LPCTSTR)strText, textSize);
                ::GlobalUnlock(hClipbuffer);
            }
            ::SetClipboardData(AFX_TCF_TEXT, hClipbuffer);
        }
        CloseClipboard();
    }
}


CString getScriptTempPath()
{
    TCHAR szTempPath[MAX_PATH];
    GetTempPath(MAX_PATH, szTempPath);
    PathAppend(szTempPath, _T("ffmpeg-video-conversion-script.cmd"));
    return szTempPath;
}


// A function that locks a file for writing but leaves it unmodified
HANDLE lockFile(LPCTSTR fileName)
{
    // Try to open the file with exclusive access
    HANDLE hFile = CreateFile(fileName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD dwError = GetLastError();

    // If the file does not exist, create a new one
    if (dwError == ERROR_FILE_NOT_FOUND)
    {
        hFile = CreateFile(fileName, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        dwError = GetLastError();
    }

    // If the file is locked by another process, report an error
    if (dwError == ERROR_SHARING_VIOLATION)
    {
        TRACE("The file is locked by another process.\n");
        return NULL;
    }

    // If any other error occurs, report an error
    if (hFile == INVALID_HANDLE_VALUE)
    {
        TRACE("Unable to lock the file. Error code: %d.\n", dwError);
        return NULL;
    }

    // Return the handle to the file
    return hFile;
}

// A function that overwrites the file with some stuff
void writeFile(HANDLE hFile, const char* stuff, DWORD size)
{
    // Move the file pointer to the beginning of the file
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    // Write the stuff to the file
    DWORD dwWritten = 0;
    WriteFile(hFile, stuff, size, &dwWritten, NULL);

    // Check if the write operation was successful
    if (dwWritten != size)
    {
        TRACE("Unable to write to the file.\n");
    }

    // Truncate the file to the current position of the file pointer
    SetEndOfFile(hFile);
}


// A function that ensures that a CString based file path ends with a file path separator
void ensureSeparator(CString& filePath)
{
    // Remove any trailing whitespace from the file path
    filePath.TrimRight();

    // Get the length of the file path
    int length = filePath.GetLength();

    // If the file path is not empty
    if (length > 0)
    {
        // Get the last character of the file path
        const char lastChar = filePath.GetAt(length - 1);

        // If the last character is not the separator character
        if (lastChar != '\\' && lastChar != '/')
        {
            // Append the separator character to the file path
            filePath.Append(_T("\\"));
        }
    }
}


std::unique_ptr<IAudioPlayer> GetAudioPlayer()
{
    if (IsWindowsVistaOrGreater())
        return std::make_unique<AudioPlayerWasapi>();
    return std::make_unique<AudioPlayerImpl>();
}       

template<typename T>
auto GetAddToSubtitlesMapLambda(T& map)
{
    return [&map](double start, double end, const std::string& subtitle) {
        map->add({ boost::icl::interval<double>::closed(start, end), subtitle });
    };
}

template <typename T> T reversed(const T& s)
{
    return { s.rbegin(), s.rend() };
}

CCriticalSection s_csSubtitles;

} // namespace


class CPlayerDoc::SubtitlesMap : public boost::icl::interval_map<double, std::string>
{
public:
    bool m_unicodeSubtitles = false;
};

class CPlayerDoc::StringDifference
{
    typedef std::basic_string<TCHAR> Sequence;

    dtl::Diff<TCHAR, std::basic_string<TCHAR>> m_diff, m_reversedDiff;
    std::filesystem::path m_parent_path;
    std::filesystem::path m_extension;

    static auto SafePathString(std::basic_string<TCHAR> path)
    {
        enum { MAX_DIFF_SIZE = 2048 };
        path.append(MAX_DIFF_SIZE, _T('\0'));
        return path;
    }

public:
    StringDifference(const Sequence& a, const Sequence& b)
        : m_diff(a, b)
        , m_reversedDiff(reversed(a), reversed(b))
    {
        std::filesystem::path path_b(b);
        if (path_b.has_parent_path() && path_b.has_stem() && path_b.stem() == std::filesystem::path(a).stem())
        {
            m_parent_path = path_b.parent_path();
            m_extension = path_b.extension();
        }
        else
        {
            m_diff.compose();
            m_reversedDiff.compose();
        }
    }

    Sequence patch(const Sequence& seq) const
    {
        if (m_parent_path.empty())
        {
            Sequence s = m_reversedDiff.patch(SafePathString(reversed(seq)));
            std::reverse(s.begin(), s.begin() + _tcslen(s.c_str()));
            if (!s.empty() && s[0] != 0 && 0 == _taccess(s.c_str(), 04)) {
                return s;
            }

            return m_diff.patch(SafePathString(seq));
        }

        return (m_parent_path / std::filesystem::path(seq).stem()) += m_extension;
    }
};

// CPlayerDoc

IMPLEMENT_DYNCREATE(CPlayerDoc, CDocument)

BEGIN_MESSAGE_MAP(CPlayerDoc, CDocument)
    ON_COMMAND_RANGE(ID_TRACK1, ID_TRACK1 + 99, OnAudioTrack)
    ON_UPDATE_COMMAND_UI_RANGE(ID_TRACK1, ID_TRACK1 + 99, OnUpdateAudioTrack)
    ON_COMMAND_RANGE(ID_VIDEO_SPEED1, ID_NIGHTCORE, OnVideoSpeed)
    ON_UPDATE_COMMAND_UI_RANGE(ID_VIDEO_SPEED1, ID_NIGHTCORE, OnUpdateVideoSpeed)
    ON_COMMAND(ID_AUTOPLAY, &CPlayerDoc::OnAutoplay)
    ON_UPDATE_COMMAND_UI(ID_AUTOPLAY, &CPlayerDoc::OnUpdateAutoplay)
    ON_COMMAND(ID_LOOPING, &CPlayerDoc::OnLooping)
    ON_UPDATE_COMMAND_UI(ID_LOOPING, &CPlayerDoc::OnUpdateLooping)
    ON_COMMAND(ID_FILE_SAVE_COPY_AS, &CPlayerDoc::OnFileSaveCopyAs)
    ON_COMMAND(ID_OPENSUBTITLESFILE, &CPlayerDoc::OnOpensubtitlesfile)
    ON_UPDATE_COMMAND_UI(ID_OPENSUBTITLESFILE, &CPlayerDoc::OnUpdateOpensubtitlesfile)
    ON_COMMAND(ID_COPY_URL_TO_CLIPBOARD, &CPlayerDoc::OnCopyUrlToClipboard)
    ON_COMMAND(ID_MAXIMALRESOLUTION, &CPlayerDoc::OnMaximalresolution)
    ON_UPDATE_COMMAND_UI(ID_MAXIMALRESOLUTION, &CPlayerDoc::OnUpdateMaximalresolution)
    ON_COMMAND(ID_HW_ACCELERATION, &CPlayerDoc::OnHwAcceleration)
    ON_UPDATE_COMMAND_UI(ID_HW_ACCELERATION, &CPlayerDoc::OnUpdateHwAcceleration)
    ON_COMMAND_RANGE(ID_FIRST_SUBTITLE, ID_FIRST_SUBTITLE+99, OnGetSubtitles)
    ON_UPDATE_COMMAND_UI_RANGE(ID_FIRST_SUBTITLE, ID_FIRST_SUBTITLE + 99, OnUpdateOpensubtitlesfile)
    ON_COMMAND(ID_SUPER_RESOLUTION, &CPlayerDoc::OnSuperResolution)
    ON_UPDATE_COMMAND_UI(ID_SUPER_RESOLUTION, &CPlayerDoc::OnUpdateSuperResolution)
    ON_COMMAND(ID_ORIENTATION_MIRRORX, &CPlayerDoc::OnOrientationMirrorx)
    ON_UPDATE_COMMAND_UI(ID_ORIENTATION_MIRRORX, &CPlayerDoc::OnUpdateOrientationMirrorx)
    ON_COMMAND(ID_ORIENTATION_MIRRORY, &CPlayerDoc::OnOrientationMirrory)
    ON_UPDATE_COMMAND_UI(ID_ORIENTATION_MIRRORY, &CPlayerDoc::OnUpdateOrientationMirrory)
    ON_COMMAND(ID_ORIENTATION_UPEND, &CPlayerDoc::OnOrientationUpend)
    ON_UPDATE_COMMAND_UI(ID_ORIENTATION_UPEND, &CPlayerDoc::OnUpdateOrientationUpend)
    ON_COMMAND(ID_ORIENTATION_DO_NOTHING, &CPlayerDoc::OnOrientationDoNothing)
    ON_COMMAND(ID_ORIENTATION_RORATE_90, &CPlayerDoc::OnOrientationRotate90)
    ON_COMMAND(ID_ORIENTATION_RORATE_180, &CPlayerDoc::OnOrientationRotate180)
    ON_COMMAND(ID_ORIENTATION_RORATE_270, &CPlayerDoc::OnOrientationRotate270)
    ON_COMMAND(ID_FIX_ENCODING, &CPlayerDoc::OnFixEncoding)
    ON_UPDATE_COMMAND_UI(ID_FIX_ENCODING, &CPlayerDoc::OnUpdateFixEncoding)
    ON_COMMAND(ID_CONVERT_VIDEOS_INTO_COMPATIBLE_FORMAT,
               &CPlayerDoc::OnConvertVideosIntoCompatibleFormat)
    ON_UPDATE_COMMAND_UI(ID_CONVERT_VIDEOS_INTO_COMPATIBLE_FORMAT,
                         &CPlayerDoc::OnUpdateConvertVideosIntoCompatibleFormat)
    END_MESSAGE_MAP()


// CPlayerDoc construction/destruction

CPlayerDoc::CPlayerDoc()
    : m_frameDecoder(
        GetFrameDecoder(
            std::make_unique<AudioPitchDecorator>(GetAudioPlayer(),
            std::bind(&CPlayerDoc::getVideoSpeed, this))))
{
    m_frameDecoder->setDecoderListener(this);
}

CPlayerDoc::~CPlayerDoc()
{
    onDestructing();

    ASSERT(framePositionChanged.empty());
    ASSERT(startTimeUpdated.empty());
    ASSERT(totalTimeUpdated.empty());
    ASSERT(currentTimeUpdated.empty());

    ASSERT(rangeStartTimeChanged.empty());
    ASSERT(rangeEndTimeChanged.empty());
}

BOOL CPlayerDoc::OnNewDocument()
{
    // (SDI documents will reuse this document)
    if (AfxGetApp()->m_pMainWnd) // false if the document is being initialized for the first time
    {
        CDialogOpenURL dlg;
        if (dlg.DoModal() == IDOK && !dlg.m_URL.IsEmpty() && CDocument::OnNewDocument())
        {
            reset();
            if (!dlg.m_inputFormt.IsEmpty())
            {
                std::string url(CT2A(dlg.m_URL, CP_UTF8));
                std::string inputFormat(CT2A(dlg.m_inputFormt, CP_UTF8));
                return openUrl(url, inputFormat);
            }
            return openTopLevelUrl(dlg.m_URL, dlg.m_bParse);
        }

        return false;
    }
    else if (GetKeyState(VK_SHIFT) < 0 && GetKeyState(VK_CONTROL) < 0)
    {
        HRSRC hFound = ::FindResource(NULL, MAKEINTRESOURCE(IDR_LAUNCH), RT_RCDATA);
        HGLOBAL hRes = ::LoadResource(NULL, hFound);

        if (!m_frameDecoder->openStream(std::make_unique<ByteStreamBuffer>(
            (char*)::LockResource(hRes), ::SizeofResource(NULL, hFound))))
        {
            return false;
        }

        m_frameDecoder->play(true);
        UpdateAllViews(nullptr, UPDATE_HINT_CLOSING);
        m_frameDecoder->pauseResume();
        onPauseResume(false);
        return true;
    }

    return CDocument::OnNewDocument();
}

bool CPlayerDoc::openTopLevelUrl(const CString& topLevelUrl, bool force, const CString& pathName)
{
    std::string url(CT2A(topLevelUrl, CP_UTF8));

    auto playList = ParsePlaylist(url, force);

    if (!playList.empty())
    {
        if (openUrlFromList(playList, pathName))
            return true;
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

bool CPlayerDoc::openUrl(const std::string& originalUrl, const std::string& inputFormat)
{
    std::pair<std::string, std::string> urls;

    if (!inputFormat.empty())
    {
        if (!m_frameDecoder->openUrls({ originalUrl }, inputFormat))
            return false;
        urls.first = originalUrl;
    }
    else
    {
        urls = getYoutubeUrl(originalUrl, m_maximalResolution);
        if (urls.first.empty() || !((m_maximalResolution && !urls.second.empty())
            ? m_frameDecoder->openUrls({ urls.first, urls.second })
            : m_frameDecoder->openUrls({ urls.first })))
        {
            return false;
        }
    }

    m_frameDecoder->play(true);
    m_originalUrl = originalUrl;
    m_url = urls.first;

    if (m_maximalResolution && !urls.second.empty()) {
        m_separateFileDiff = std::make_unique<StringDifference>(
            std::basic_string<TCHAR>(urls.first.begin(), urls.first.end()),
            std::basic_string<TCHAR>(urls.second.begin(), urls.second.end()));
    }
    else {
        m_separateFileDiff.reset();
    }

    m_subtitles.reset();
    m_nightcore = false;
    ++m_documentGeneration;
    UpdateAllViews(nullptr, UPDATE_HINT_CLOSING);

    if (inputFormat.empty())
    {
        auto map(std::make_unique<SubtitlesMap>());
        if (getYoutubeTranscripts(originalUrl,
            [&map](double start, double duration, const std::string& text) {
            map->add({
                boost::icl::interval<double>::closed(start, start + duration),
                boost::algorithm::trim_copy(text) + '\n' });
        }))
        {
            map->m_unicodeSubtitles = true;
            m_subtitles = std::move(map);
        }
    }
    m_frameDecoder->pauseResume();
    onPauseResume(false);
    return true;
}

bool CPlayerDoc::openUrlFromList()
{
    bool networkCkecked = false;

    while (!m_playList.empty())
    {
        auto buffer = m_playList.front();
        m_playList.pop_front();

        auto playList = ParsePlaylist(buffer, false);
        if (!playList.empty())
            m_playList.insert(m_playList.begin(), playList.begin(), playList.end());
        else if (openUrl(buffer))
            return true;
        else if (!networkCkecked && PathIsURLA(buffer.c_str()))
        {
            networkCkecked = true;
            DWORD flags = NETWORK_ALIVE_INTERNET;
            if (!IsNetworkAlive(&flags))
            {
                m_playList.push_front(buffer);
                return false;
            }
        }

        if (!m_playList.empty())
        {
            const auto documentGeneration = m_documentGeneration;

            if (!PumpMessages())
                return false;

            if (m_playList.empty() || documentGeneration != m_documentGeneration)
                AfxThrowUserException();
        }
    }
    return false;
}

bool CPlayerDoc::openUrlFromList(const std::vector<std::string>& playList, const CString& pathName)
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
    return false;
}

void CPlayerDoc::reset()
{
    m_frameDecoder->close();
    m_subtitles.reset();
    m_reopenFunc = nullptr;

    m_originalUrl.clear();
    m_url.clear();

    m_nightcore = false;

    ++m_documentGeneration;

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
    const bool shiftAndControlPressed = GetKeyState(VK_SHIFT) < 0
        && GetKeyState(VK_CONTROL) < 0;
    if (shiftAndControlPressed && IsCalledFromMruList())
        return false;

    return openDocument(lpszPathName, shiftAndControlPressed);
}

BOOL CPlayerDoc::OnSaveDocument(LPCTSTR lpszPathName)
{
    const bool isLocalFile = m_url.empty();

    if (isLocalFile && !_tcsicmp(m_strPathName, lpszPathName))
    {
        return false;
    }

    CString source(isLocalFile
        ? m_strPathName
        : CString(m_url.data(), m_url.length()));

    if (source.IsEmpty())
        return false;

    const bool transform = m_bOrientationMirrorx || m_bOrientationMirrory || m_bOrientationUpend;

    if (isLocalFile && isFullFrameRange() && !transform)
    {
        return CopyFile(source, lpszPathName, FALSE); // overwrites the existing file
    }

    CString strFile;
    CString strParams;
    if (isFullFrameRange() && !m_separateFileDiff && !transform)
    {
        strFile = _T("HttpDownload.exe");
        strParams = source + _T(" \"") + lpszPathName + _T('"');
    }
    else
    {
        CString timeClause;
        if (!isFullFrameRange())
        {
            timeClause.Format(
                _T("-ss %.3f -t %.3f -accurate_seek "),
                m_rangeStartTime,
                m_rangeEndTime - m_rangeStartTime);

        }
        strFile = _T("ffmpeg.exe");
        strParams = timeClause + _T("-i \"") + source + _T('"');

        if (m_separateFileDiff)
        {
            const auto s = m_separateFileDiff->patch({ source.GetString(), source.GetString() + source.GetLength() });
            if (!s.empty()) {
                strParams += _T(" ") + timeClause + _T("-i \"") + s.c_str() 
                    + _T("\" -map 0:v:0 -map 1:a:0");
            }
        }

        const bool streamcopy = (m_losslessCut || isFullFrameRange()) && !transform;

        if (streamcopy)
            strParams += _T(" -c copy");

        // rotation https://webcache.googleusercontent.com/search?q=cache:IiCyGV1Tp7oJ:https://annimon.com/article/3997+
        if (m_bOrientationUpend)
            if (m_bOrientationMirrory)
                strParams += m_bOrientationMirrorx ? _T(" -vf transpose=3") : _T(" -vf transpose=1");
            else 
                strParams += m_bOrientationMirrorx ? _T(" -vf transpose=2") : _T(" -vf transpose=0");
        else if (m_bOrientationMirrory)
            strParams += m_bOrientationMirrorx ? _T(" -vf hflip,vflip") : _T(" -vf vflip");
        else if (m_bOrientationMirrorx)
            strParams += _T(" -vf hflip");

        if (!isFullFrameRange())
        {
            strParams += _T(" -avoid_negative_ts 1 -map_chapters -1");
        }
        if (!streamcopy)
            strParams += _T(" -q:v 4");
        strParams += _T(" -y \"");
        strParams += lpszPathName;
        strParams += _T('"');
        TRACE(_T("FFmpeg parameters generated: %s\n"), strParams);
    }
    TCHAR pszPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, pszPath, ARRAYSIZE(pszPath));
    PathRemoveFileSpec(pszPath);
    const  auto result = ShellExecute(NULL, NULL, strFile, strParams, pszPath, SW_MINIMIZE);
    return int(result) > 32;
}


bool CPlayerDoc::openDocument(LPCTSTR lpszPathName, bool openSeparateFile /*= false*/)
{
    reset();

    CString currentDirectory;
    if (auto fileName = PathFindFileName(lpszPathName))
    {
        currentDirectory = CString(lpszPathName, fileName - lpszPathName);
        SetCurrentDirectory(currentDirectory);
    }

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
        // https://community.spiceworks.com/topic/1968971-opening-web-links-downloading-1-item-to-zcrksihu
        if (extension[0] == _T('\0') && (_tcsstr(lpszPathName, _T("playlist")) || _tcsstr(lpszPathName, _T("watch")))
            || !_tcsicmp(extension, _T(".html")) || !_tcsicmp(extension, _T(".txt")))
        {
            auto playList = ParsePlaylistFile(lpszPathName);
            if (!playList.empty())
            {
                return openUrlFromList(playList);
            }
        }

        CString mappedAudioFile;
        if (openSeparateFile) {
            if (!AfxGetApp()->m_pMainWnd && AfxGetMainWnd()) {
                SetForegroundWindowInternal(*AfxGetMainWnd());
                AfxGetMainWnd()->ShowWindow(SW_SHOW);
            }
            CFileDialog dlg(TRUE);
            if (!currentDirectory.IsEmpty())
                dlg.GetOFN().lpstrInitialDir = currentDirectory;
            if (dlg.DoModal() != IDOK)
            {
                return false;
            }
            mappedAudioFile = dlg.GetPathName();
            static_cast<CPlayerApp*>(AfxGetApp())->SetMappedAudioFile(lpszPathName, dlg.GetPathName());
        }
        else {
            mappedAudioFile = static_cast<CPlayerApp*>(AfxGetApp())->GetMappedAudioFile(lpszPathName);
        }

        if (!mappedAudioFile.IsEmpty()) {
            m_separateFileDiff = std::make_unique<StringDifference>(
                lpszPathName, static_cast<LPCTSTR>(mappedAudioFile));
        }
        else if (m_autoPlay && m_separateFileDiff) {
            const auto s = m_separateFileDiff->patch(lpszPathName);
            if (!s.empty() && 0 != _tcscmp(s.c_str(), lpszPathName) && 0 == _taccess(s.c_str(), 04)) {
                mappedAudioFile = s.c_str();
            }
            else {
                m_separateFileDiff.reset();
            }
        }
        else {
            m_separateFileDiff.reset();
        }

        std::string pathNameA(CT2A(lpszPathName, CP_UTF8));
        if (!mappedAudioFile.IsEmpty())
        {
            if (!m_frameDecoder->openUrls(
                    {pathNameA, std::string(CT2A(mappedAudioFile, CP_UTF8))})) 
            {
                return false;
            }
        }
        else if (!m_frameDecoder->openUrls({pathNameA}))
            return false;
        m_playList.clear();

        m_subtitles.reset();

        if (m_autoPlay && m_subtitlesFileDiff) {
            const auto s = m_subtitlesFileDiff->patch(lpszPathName);
            auto map(std::make_unique<SubtitlesMap>());
            if (!s.empty() && 0 != _tcscmp(s.c_str(), lpszPathName)
                    && OpenSubtitlesFile(s.c_str(), map->m_unicodeSubtitles, GetAddToSubtitlesMapLambda(map))) {
                m_subtitles = std::move(map);
            }
            else {
                m_subtitlesFileDiff.reset();
            }
        }
        else {
            m_subtitlesFileDiff.reset();
        }

        if (!m_subtitles) {
            auto map(std::make_unique<SubtitlesMap>());
            if (OpenMatchingSubtitlesFile(lpszPathName, map->m_unicodeSubtitles, GetAddToSubtitlesMapLambda(map)))
            {
                m_subtitles = std::move(map);
            }
        }

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
        TRACE("Warning: File save-as failed.\n");
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

void CPlayerDoc::SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU) 
{
    if (_tcslen(lpszPathName) < _MAX_PATH)
    {
        __super::SetPathName(lpszPathName, bAddToMRU);
        return;
    }

    m_strPathName = lpszPathName;

    ASSERT(!m_strPathName.IsEmpty());  // must be set to something
    m_bEmbedded = FALSE;

    // set the document title based on path name
    TCHAR szTitle[_MAX_FNAME];
    if (::GetFileTitle(lpszPathName, szTitle, _countof(szTitle)) == 0)
    {
        SetTitle(szTitle);
    }
    else 
    {
        SetTitle(::PathFindFileName(lpszPathName));
    }

    // add it to the file MRU list
    //if (bAddToMRU)
    //    AfxGetApp()->AddToRecentFileList(m_strPathName);
}


void CPlayerDoc::changedFramePosition(long long start, long long frame, long long total)
{
    framePositionChanged(frame - start, total - start);
    const double currentTime = m_frameDecoder->getDurationSecs(frame);
    m_currentTime = currentTime;
    currentTimeUpdated(currentTime);

    if (m_looping && !m_autoPlay && !isFullFrameRange() && m_currentTime >= m_rangeEndTime &&
        m_endTime > m_startTime)
    {
        const double percent = (m_rangeStartTime - m_startTime) / (m_endTime - m_startTime);
        m_frameDecoder->seekByPercent(percent);
    }
}

void CPlayerDoc::decoderClosed(bool /*fileReleased*/)
{
    m_onEndOfStream = false;
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

void CPlayerDoc::onEndOfStream(int idx, bool error)
{
    if (idx != 0)
        return;

    if (!error && m_looping && !m_autoPlay && !isFullFrameRange() && m_endTime > m_startTime)
    {
        const double percent = (m_rangeStartTime - m_startTime) / (m_endTime - m_startTime);
        if (m_frameDecoder->seekByPercent(percent))
            return;
    }

    if (!m_onEndOfStream)
    {
        m_onEndOfStream = true;
        if (CWnd* pMainWnd = AfxGetApp()->GetMainWnd())
            pMainWnd->PostMessage(WM_KICKIDLE);  // trigger idle update
    }
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

bool CPlayerDoc::prevFrame()
{
    return m_frameDecoder->prevFrame();
}

bool CPlayerDoc::seekByPercent(double percent)
{
    return m_frameDecoder->seekByPercent(percent);
}

void CPlayerDoc::seekToEnd()
{
    if (m_autoPlay && m_currentTime - m_startTime > 5) // enable after 5 seconds
    {
        m_onEndOfStream = false;
        MoveToNextFile();
    }
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


std::wstring CPlayerDoc::getSubtitle() const
{
    std::wstring result;
    {
        CSingleLock lock(&s_csSubtitles, TRUE);
        if (m_subtitles)
        {
            auto it = m_subtitles->find(m_currentTime);
            if (it != m_subtitles->end())
            {
                if (m_subtitles->m_unicodeSubtitles && m_bFixEncoding)
                {
                    CA2W bufW(it->second.c_str(), CP_UTF8);
                    CW2A bufA(bufW, CP_ACP);
                    result = CA2W(bufA, CP_UTF8);
                }
                else
                {
                    result = CA2W(it->second.c_str(),
                                  m_subtitles->m_unicodeSubtitles ? CP_UTF8 : CP_ACP);
                }
            }
        }
    }

    if (!result.empty())
    {
        using std::wregex;
        // Replace any whitespace followed by a newline with an empty string
        result = regex_replace(result, wregex(L"\\s+(?=\\n)"), L"");
        // Replace punctuation followed by whitespace with the punctuation followed by a Unicode
        // thin space character
        result = regex_replace(result, wregex(L"([,.?!;:])\\s"), L"$1\u2009");
        // Replace an ellipsis occurring after a non-period character or at the start of the string
        // with a Unicode ellipsis character
        result = regex_replace(result, wregex(L"(^|[^.])\\.{3}([^.]|$)"), L"$1\u2026$2");
    }

    return result;
}


void CPlayerDoc::setRangeStartTime(double time)
{
    if (time < min(0., m_startTime))
        time = m_endTime + time;
    m_rangeStartTime = time;
    rangeStartTimeChanged(time - m_startTime, m_endTime - m_startTime);
}

void CPlayerDoc::setRangeEndTime(double time)
{
    if (time <= 0)
        time = m_endTime + time;
    m_rangeEndTime = time;
    rangeEndTimeChanged(time - m_startTime, m_endTime - m_startTime);
}

void CPlayerDoc::setLosslessCut(bool flag)
{
    m_losslessCut = flag;
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
    const UINT cFiles = DragQueryFile(hDropInfo, (UINT)-1, NULL, 0);
    if (cFiles == 0)
        return;

    if (cFiles == 1)
    {
        TCHAR lpszFileName[MAX_PATH];
        if (DragQueryFile(hDropInfo, 0, lpszFileName, MAX_PATH)
            && openDocument(lpszFileName))
        {
            SetPathName(lpszFileName, TRUE);
        }
    }
    else
    {
        std::vector<std::string> playList;
        for (UINT i = 0; i < cFiles; ++i)
        {
            TCHAR lpszFileName[MAX_PATH]{};
            if (DragQueryFile(hDropInfo, i, lpszFileName, MAX_PATH))
                playList.emplace_back(CT2A(lpszFileName, CP_UTF8));
        }
        if (!playList.empty())
        {
            GetDocTemplate()->SetDefaultTitle(this);
            ClearPathName();
            openUrlFromList(playList);
        }
    }
}

void CPlayerDoc::OnEditPaste(const std::string& text)
{
    auto playList = ParsePlaylistText(text);
    if (!playList.empty())
    {
        GetDocTemplate()->SetDefaultTitle(this);
        ClearPathName();
        openUrlFromList(playList);
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
    return static_cast<float>(speedRational.denominator) / speedRational.numerator;
}


void CPlayerDoc::OnOpensubtitlesfile()
{
    CFileDialog dlg(TRUE); // TODO extensions
    CString currentDirectory;
    if (auto fileName = PathFindFileName(static_cast<LPCTSTR>(m_strPathName)))
    {
        currentDirectory = CString(static_cast<LPCTSTR>(m_strPathName),
            fileName - static_cast<LPCTSTR>(m_strPathName));
        dlg.GetOFN().lpstrInitialDir = currentDirectory;
    }
    if (dlg.DoModal() != IDOK)
    {
        return;
    }

    auto map(std::make_unique<SubtitlesMap>());
    if (OpenSubtitlesFile(dlg.GetPathName(), map->m_unicodeSubtitles, GetAddToSubtitlesMapLambda(map)))
    {
        m_subtitlesFileDiff = std::make_unique<StringDifference>(
            static_cast<LPCTSTR>(m_strPathName), static_cast<LPCTSTR>(dlg.GetPathName()));
        CSingleLock lock(&s_csSubtitles, TRUE);
        m_subtitles = std::move(map);
    }
}


void CPlayerDoc::OnUpdateOpensubtitlesfile(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(isPlaying() && m_url.empty());
}

void CPlayerDoc::OnCopyUrlToClipboard()
{
    const bool shiftAndControlPressed = GetKeyState(VK_SHIFT) < 0
        && GetKeyState(VK_CONTROL) < 0;

    const auto& url = shiftAndControlPressed ? m_url : m_originalUrl;

    CString strText(url.empty() ? m_strPathName : CString(url.data(), url.length()));

    CopyTextToClipboard(strText);
}


void CPlayerDoc::OnMaximalresolution()
{
    m_maximalResolution = !m_maximalResolution;
}


void CPlayerDoc::OnUpdateMaximalresolution(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_maximalResolution);
}


void CPlayerDoc::OnHwAcceleration()
{
    m_frameDecoder->setHwAccelerated(!m_frameDecoder->getHwAccelerated());
}


void CPlayerDoc::OnUpdateHwAcceleration(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_frameDecoder->getHwAccelerated());
}

void CPlayerDoc::OnGetSubtitles(UINT id)
{
    typedef std::pair<CPlayerDoc*, int> GetSubtitlesParams;

    auto threadLam = [](LPVOID pParam) {
        std::unique_ptr<GetSubtitlesParams> params(static_cast<GetSubtitlesParams*>(pParam));

        auto map(std::make_shared<SubtitlesMap>());

        map->m_unicodeSubtitles = true;

        auto addToSubtitlesLam = [weakPtr = std::weak_ptr<SubtitlesMap>(map)](
                double start, double end, const std::string& subtitle) {
            if (auto map = weakPtr.lock()) {
                CSingleLock lock(&s_csSubtitles, TRUE);
                map->set({ boost::icl::interval<double>::closed(start, end), subtitle });
                return true;
            }
            return false;
        };

        {
            CSingleLock lock(&s_csSubtitles, TRUE);
            params->first->m_subtitles = std::move(map);
        }
        params->first->m_frameDecoder->getSubtitles(params->second, addToSubtitlesLam);

        return UINT();
    };

    AfxBeginThread(threadLam, new GetSubtitlesParams(this, id - ID_FIRST_SUBTITLE));
}


void CPlayerDoc::OnSuperResolution()
{
    m_superResolution = !m_superResolution;
    if (m_superResolution)
    {
        CWaitCursor wait;
        EnableImageUpscale();
        m_frameDecoder->setImageConversionFunc(ImageUpscale);
    }
    else
        m_frameDecoder->setImageConversionFunc({});
}


void CPlayerDoc::OnUpdateSuperResolution(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(CanUpscaleImage());
    pCmdUI->SetCheck(m_superResolution);
}


void CPlayerDoc::OnOrientationMirrorx()
{
    m_bOrientationMirrorx = !m_bOrientationMirrorx;
    AfxGetApp()->GetMainWnd()->Invalidate();
}

void CPlayerDoc::OnUpdateOrientationMirrorx(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_bOrientationMirrorx);
}


void CPlayerDoc::OnOrientationMirrory()
{
    m_bOrientationMirrory = !m_bOrientationMirrory;
    AfxGetApp()->GetMainWnd()->Invalidate();
}


void CPlayerDoc::OnUpdateOrientationMirrory(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_bOrientationMirrory);
}


void CPlayerDoc::OnOrientationUpend()
{
    m_bOrientationUpend = !m_bOrientationUpend;
    AfxGetApp()->GetMainWnd()->Invalidate();
}


void CPlayerDoc::OnUpdateOrientationUpend(CCmdUI *pCmdUI)
{
    pCmdUI->SetCheck(m_bOrientationUpend);
}

void CPlayerDoc::OnOrientationDoNothing()
{
    m_bOrientationMirrorx = false;
    m_bOrientationMirrory = false;
    m_bOrientationUpend = false;
    AfxGetApp()->GetMainWnd()->Invalidate();
}

void CPlayerDoc::OnOrientationRotate90()
{
    m_bOrientationMirrorx = false;
    m_bOrientationMirrory = true;
    m_bOrientationUpend = true;
    AfxGetApp()->GetMainWnd()->Invalidate();
}

void CPlayerDoc::OnOrientationRotate180()
{
    m_bOrientationMirrorx = true;
    m_bOrientationMirrory = true;
    m_bOrientationUpend = false;
    AfxGetApp()->GetMainWnd()->Invalidate();
}

void CPlayerDoc::OnOrientationRotate270()
{
    m_bOrientationMirrorx = true;
    m_bOrientationMirrory = false;
    m_bOrientationUpend = true;
    AfxGetApp()->GetMainWnd()->Invalidate();
}

void CPlayerDoc::OnFixEncoding()
{ 
    m_bFixEncoding = !m_bFixEncoding;
}

void CPlayerDoc::OnUpdateFixEncoding(CCmdUI* pCmdUI)
{
   pCmdUI->SetCheck(m_bFixEncoding); 
}

void CPlayerDoc::OnConvertVideosIntoCompatibleFormat()
{
    const auto scriptTempPath = getScriptTempPath();

    {
        const auto scriptFileHandle = lockFile(scriptTempPath);
        if (!scriptFileHandle)
        {
            return;
        }

        std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&::CloseHandle)> scriptFileGuard(
            scriptFileHandle, ::CloseHandle);

        // choose output folder using CFolderPickerDialog
        CString outputFolder;
        {
            CFolderPickerDialog dlg;
            if (dlg.DoModal() != IDOK)
            {
                return;
            }

            outputFolder = dlg.GetPathName();
        }

        ensureSeparator(outputFolder);

        std::vector<CString> videoFiles;
        if (m_autoPlay)
        {
            if (!m_looping)
            {
                videoFiles.push_back(GetPathName());
            }

            HandleFilesSequence(
                GetPathName(), m_looping,
                [&videoFiles](const CString& path)
                {
                    videoFiles.push_back(path);
                    return false;
                },
                m_looping);
        }
        else
        {
            videoFiles.push_back(GetPathName());
        }

        const bool isVideoCompatible = m_frameDecoder->isVideoCompatible();

        CString ffmpegPath;
        {
            TCHAR pszPath[MAX_PATH] = {0};
            GetModuleFileName(NULL, pszPath, ARRAYSIZE(pszPath));
            PathRemoveFileSpec(pszPath);
            const TCHAR ffmpegExeName[] = _T("ffmpeg.exe");
            PathAppend(pszPath, ffmpegExeName);
            ffmpegPath = (_taccess(pszPath, 04) == 0) 
                ? (_T('"') + CString(pszPath) + _T('"')) : ffmpegExeName;
        }

        CString strText(_T("chcp 65001\r\n"));

        for (const auto& source : videoFiles)
        {
            CString command = ffmpegPath + _T(" -i \"") + source + _T('"');

            std::basic_string<TCHAR> separateFilePart;
            if (m_separateFileDiff)
            {
                auto s = m_separateFileDiff->patch(
                    {source.GetString(), source.GetString() + source.GetLength()});
                if (!s.empty())
                {
                    separateFilePart =
                        _T(" -i \"") + std::move(s) + _T("\" -map 0:v:0 -map 1:a:0");
                }
            }

            if (separateFilePart.empty())
            {
                command += _T(" -map 0:v? -map 0:a?");
            }
            else
            {
                command += separateFilePart.c_str();
            }

            command += _T(" -map 0:s?");

            command += isVideoCompatible ? _T(" -c:v copy")
                                         : _T(" -c:v libx264 -crf 25 -pix_fmt yuv420p");
            command += _T(" -c:a aac -ac 2 -c:s copy -preset superfast \"");
            command += outputFolder;
            command += ::PathFindFileName(source);
            command += _T("\"\r\n");

            strText += command;
        }

        if (m_subtitlesFileDiff)
        {
            for (const auto& source : videoFiles)
            {
                const auto s = m_subtitlesFileDiff->patch(
                    {source.GetString(), source.GetString() + source.GetLength()});
                if (s.empty())
                    continue;

                const auto audioExt = ::PathFindExtension(s.c_str());

                const auto fileNameWithExt = ::PathFindFileName(source);
                std::basic_string<TCHAR> fileName{fileNameWithExt,
                                                  ::PathFindExtension(fileNameWithExt)};

                auto command =
                    _T("copy \"") + s + _T("\" \"") + fileName + audioExt + _T("\"\r\n");
                strText += command.c_str();
            }
        }

        CW2A bufA(strText, CP_UTF8);
        writeFile(scriptFileHandle, bufA, strlen(bufA));
    }

    // Try to execute the file with the "open" verb
    HINSTANCE hResult =
        ShellExecute(NULL, _T("open"), scriptTempPath, NULL, NULL, SW_MINIMIZE);
}



void CPlayerDoc::OnUpdateConvertVideosIntoCompatibleFormat(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!GetPathName().IsEmpty()); 
}
