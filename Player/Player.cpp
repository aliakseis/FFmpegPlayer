
// Player.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "Player.h"
#include "MainFrm.h"

#include "PlayerDoc.h"
#include "PlayerView.h"
#include "PlayerViewD2D.h"

#include "I420Effect.h"

#include "AsyncGetUrlUnderMouseCursor.h"

#include "version.h"

#include <boost/log/sinks/debug_output_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <boost/log/expressions.hpp>

#include <boost/log/trivial.hpp>

#include <boost/log/support/date_time.hpp>

#include <zlib.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//#define USE_DIRECT2D_VIEW

namespace {

// https://www.experts-exchange.com/articles/3189/In-Memory-Compression-and-Decompression-Using-ZLIB.html
int GetMaxCompressedLen(int nLenSrc)
{
    int n16kBlocks = (nLenSrc + 16383) / 16384; // round up any fraction of a block
    return (nLenSrc + 6 + (n16kBlocks * 5));
}

int CompressData(const BYTE* abSrc, int nLenSrc, BYTE* abDst, int nLenDst)
{
    z_stream zInfo { };
    zInfo.total_in = zInfo.avail_in = nLenSrc;
    zInfo.total_out = zInfo.avail_out = nLenDst;
    zInfo.next_in = const_cast<BYTE*>(abSrc);
    zInfo.next_out = abDst;

    int nRet = -1;
    int nErr = deflateInit(&zInfo, Z_BEST_COMPRESSION); // zlib function
    if (nErr == Z_OK) {
        nErr = deflate(&zInfo, Z_FINISH);              // zlib function
        if (nErr == Z_STREAM_END) {
            nRet = zInfo.total_out;
        }
    }
    deflateEnd(&zInfo);    // zlib function
    return nRet;
}

int UncompressData(const BYTE* abSrc, int nLenSrc, BYTE* abDst, int nLenDst)
{
    z_stream zInfo { };
    zInfo.total_in = zInfo.avail_in = nLenSrc;
    zInfo.total_out = zInfo.avail_out = nLenDst;
    zInfo.next_in = const_cast<BYTE*>(abSrc);
    zInfo.next_out = abDst;

    int nRet = -1;
    int nErr = inflateInit(&zInfo);               // zlib function
    if (nErr == Z_OK) {
        nErr = inflate(&zInfo, Z_FINISH);     // zlib function
        if (nErr == Z_STREAM_END) {
            nRet = zInfo.total_out;
        }
    }
    inflateEnd(&zInfo);   // zlib function
    return nRet; // -1 or len of output
}

void init_logging()
{
    namespace expr = boost::log::expressions;

    boost::log::add_common_attributes();

    auto core = boost::log::core::get();

    // Create the sink. The backend requires synchronization in the frontend.
    auto sink(boost::make_shared<boost::log::sinks::synchronous_sink<boost::log::sinks::debug_output_backend>>());

    sink->set_formatter(expr::stream
        //<< '[' << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%H:%M:%S.%f") << ']'
        << expr::if_(expr::has_attr("Severity"))
        [
            expr::stream << '[' << expr::attr< boost::log::trivial::severity_level >("Severity") << ']'
        ]
        << expr::if_(expr::has_attr("Channel"))
        [
            expr::stream << '[' << expr::attr< std::string >("Channel") << ']'
        ]
        << expr::smessage << '\n');

    // Set the special filter to the frontend
    // in order to skip the sink when no debugger is available
    //sink->set_filter(expr::is_debugger_present());

    core->add_sink(sink);
}

} // namespace


// CPlayerApp

BEGIN_MESSAGE_MAP(CPlayerApp, CWinAppEx)
    ON_COMMAND(ID_APP_ABOUT, &CPlayerApp::OnAppAbout)
    // Standard file based document commands
    ON_COMMAND(ID_FILE_NEW, &CWinAppEx::OnFileNew)
    ON_COMMAND(ID_FILE_OPEN, &CWinAppEx::OnFileOpen)
    // Standard print setup command
    ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinAppEx::OnFilePrintSetup)
    ON_THREAD_MESSAGE(WM_ON_ASYNC_URL, &CPlayerApp::OnAsyncUrl)
END_MESSAGE_MAP()


// CPlayerApp construction

CPlayerApp::CPlayerApp()
{
    // support Restart Manager
    m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;
#ifdef _MANAGED
    // If the application is built using Common Language Runtime support (/clr):
    //     1) This additional setting is needed for Restart Manager support to work properly.
    //     2) In your project, you must add a reference to System.Windows.Forms in order to build.
    System::Windows::Forms::Application::SetUnhandledExceptionMode(System::Windows::Forms::UnhandledExceptionMode::ThrowException);
#endif

    // TODO: replace application ID string below with unique ID string; recommended
    // format for string is CompanyName.ProductName.SubProduct.VersionInformation
    SetAppID(_T("Player.AppID.NoVersion"));

    // Place all significant initialization in InitInstance
    init_logging();
}

// The one and only CPlayerApp object

CPlayerApp theApp;


// CPlayerApp initialization

BOOL CPlayerApp::InitInstance()
{
    CPane::m_bHandleMinSize = true;

    // InitCommonControlsEx() is required on Windows XP if an application
    // manifest specifies use of ComCtl32.dll version 6 or later to enable
    // visual styles.  Otherwise, any window creation will fail.
    INITCOMMONCONTROLSEX InitCtrls;
    InitCtrls.dwSize = sizeof(InitCtrls);
    // Set this to include all the common control classes you want to use
    // in your application.
    InitCtrls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&InitCtrls);

    __super::InitInstance();

    AfxOleInit();

    // Parse command line for standard shell commands, DDE, file open
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (cmdInfo.m_nShellCommand == CCommandLineInfo::FileNew)
    {
        AsyncGetUrlUnderMouseCursor();
    }

#ifdef USE_DIRECT2D_VIEW
    if (AfxGetD2DState()->GetDirect2dFactory() == NULL)
    {
        return FALSE;
    }
    HRESULT hr_create = I420Effect::Register(static_cast<ID2D1Factory1*>(AfxGetD2DState()->GetDirect2dFactory()));
    if (FAILED(hr_create))
    {
        return FALSE;
    }
#endif // USE_DIRECT2D_VIEW

    EnableTaskbarInteraction(FALSE);

    // AfxInitRichEdit2() is required to use RichEdit control	
    // AfxInitRichEdit2();

    // Standard initialization
    // If you are not using these features and wish to reduce the size
    // of your final executable, you should remove from the following
    // the specific initialization routines you do not need
    // Change the registry key under which our settings are stored
    // TODO: You should modify this string to be something appropriate
    // such as the name of your company or organization
    SetRegistryKey(_T("FFMPEG Player"));
    LoadStdProfileSettings(_AFX_MRU_MAX_COUNT);  // Load standard INI file options (including MRU)

    // MFC Feature Pack
    InitContextMenuManager();
    InitShellManager();
    InitKeyboardManager();
    InitTooltipManager();
    CMFCToolTipInfo ttParams;
    ttParams.m_bVislManagerTheme = TRUE;
    theApp.GetTooltipManager()->
        SetTooltipParams(AFX_TOOLTIP_TYPE_ALL,
            RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

    // Register the application's document templates.  Document templates
    //  serve as the connection between documents, frame windows and views
    auto* pDocTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME,
        RUNTIME_CLASS(CPlayerDoc),
        RUNTIME_CLASS(CMainFrame),       // main SDI frame window
#ifdef USE_DIRECT2D_VIEW
        RUNTIME_CLASS(CPlayerViewD2D));
#else
        RUNTIME_CLASS(CPlayerView));
#endif
    if (!pDocTemplate) {
        return FALSE;
    }
    AddDocTemplate(pDocTemplate);

    HandleMruList();

    // Dispatch commands specified on the command line.  Will return FALSE if
    // app was launched with /RegServer, /Register, /Unregserver or /Unregister.
    if (!ProcessShellCommand(cmdInfo)) {
        return FALSE;
    }

    // https://stackoverflow.com/a/56079903/10472202
    //enum { TIME_PERIOD = 1 };
    //if (timeBeginPeriod(TIME_PERIOD) == TIMERR_NOERROR)
    //{
    //    atexit([] { timeEndPeriod(TIME_PERIOD); });
    //}

    // The one and only window has been initialized, so show and update it
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
}

// CPlayerApp message handlers


// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg();

    BOOL OnInitDialog() override;

    // Dialog Data
    enum { IDD = IDD_ABOUTBOX };

    CString m_videoProperties;

protected:
    void DoDataExchange(CDataExchange* pDX) override;    // DDX/DDV support

// Implementation
protected:
    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(CAboutDlg::IDD)
{
}

BOOL CAboutDlg::OnInitDialog()
{
    ModifyStyleEx(0, WS_EX_LAYERED);
    SetLayeredWindowAttributes(0, (255 * 75) / 100, LWA_ALPHA);
#ifdef GIT_COMMIT
    CString text;
    GetDlgItemText(IDC_APP_NAME_VERSION, text);
    text += " " BOOST_STRINGIZE(GIT_COMMIT);
    SetDlgItemText(IDC_APP_NAME_VERSION, text);
#endif
    return __super::OnInitDialog();
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_VIDEO_PROPERTIES, m_videoProperties);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

CPlayerDoc* CPlayerApp::GetPlayerDocument()
{
    POSITION pos1 = GetFirstDocTemplatePosition();
    if (CDocTemplate* templ = GetNextDocTemplate(pos1))
    {
        POSITION pos2 = templ->GetFirstDocPosition();
        return dynamic_cast<CPlayerDoc*>(templ->GetNextDoc(pos2));
    }

    return nullptr;
}


// App command to run the dialog
void CPlayerApp::OnAppAbout()
{
    CAboutDlg aboutDlg;

    if (CPlayerDoc* doc = GetPlayerDocument())
    {
        const auto properties = doc->getFrameDecoder()->getProperties();
        for (const auto& prop : properties)
        {
            if (!aboutDlg.m_videoProperties.IsEmpty()) {
                aboutDlg.m_videoProperties += '\n';
            }
            aboutDlg.m_videoProperties += prop.c_str();
        }
    }

    aboutDlg.DoModal();
}

// CPlayerApp message handlers

void CPlayerApp::OnAsyncUrl(WPARAM wParam, LPARAM /*unused*/)
{
    CComBSTR url;
    url.Attach((BSTR)wParam);
    if (CPlayerDoc* doc = GetPlayerDocument())
    {
        doc->OnAsyncUrl(CString(url));
    }
}


const TCHAR szMappedAudioFilesEntry[] = _T("MappedAudioFiles");
const int MappedAudioFilesEntryVersion = 2;

bool CPlayerApp::GetMappedAudioFiles(CMapStringToString& map)
{
    map.RemoveAll();

    LPBYTE pData = nullptr;
    UINT bytes = 0;
    if (!GetBinary(szMappedAudioFilesEntry, &pData, &bytes) || bytes == 0) {
        return false;
    }

    const auto size = 65536;

    std::vector<BYTE> unpacked(size);
    int nLen = UncompressData(pData, bytes, unpacked.data(), size);

    delete[] pData;

    CMemFile mf(unpacked.data(), size);
    {
        CArchive ar(&mf, CArchive::load);

        int version = 0;
        ar >> version;
        if (version != MappedAudioFilesEntryVersion) {
            return false;
        }
        map.Serialize(ar);
    }
    mf.Detach();

    return true;
}

void CPlayerApp::SetMappedAudioFiles(CMapStringToString& map)
{
    CMemFile mf;
    {
        CArchive ar(&mf, CArchive::store);
        ar << MappedAudioFilesEntryVersion;

        map.Serialize(ar);
    }

    UINT uiDataSize = static_cast<UINT>(mf.GetLength());
    LPBYTE lpbData = mf.Detach();
    if (lpbData == nullptr) {
        return;
    }

    int nLenDst = GetMaxCompressedLen(uiDataSize);
    std::vector<BYTE> packed(nLenDst);

    int nLenPacked = CompressData(lpbData, uiDataSize, packed.data(), nLenDst);
    free(lpbData);
    if (nLenPacked == -1) {
        return;  // error
    }

    WriteBinary(szMappedAudioFilesEntry, packed.data(), nLenPacked);
}

void CPlayerApp::HandleMruList()
{
    CMapStringToString oldMappedAudioFiles;
    GetMappedAudioFiles(oldMappedAudioFiles);

    CMapStringToString newMappedAudioFiles;

    if (m_pRecentFileList != nullptr)
    {
        for (int i = m_pRecentFileList->GetSize(); --i >= 0;)
        {
            const auto& key = (*m_pRecentFileList)[i];
            if (_taccess(key, 04) != 0) {
                m_pRecentFileList->Remove(i);
            }
            else
            {
                CString value;
                if (oldMappedAudioFiles.Lookup(key, value)) {
                    newMappedAudioFiles[key] = value;
                }
            }
        }
    }

    if (oldMappedAudioFiles.GetSize() != newMappedAudioFiles.GetSize()) {
        SetMappedAudioFiles(newMappedAudioFiles);
    }
}

CString CPlayerApp::GetMappedAudioFile(LPCTSTR key)
{
    CMapStringToString mappedAudioFiles;
    GetMappedAudioFiles(mappedAudioFiles);
    CString result;
    mappedAudioFiles.Lookup(key, result);
    return result;
}

void CPlayerApp::SetMappedAudioFile(LPCTSTR key, LPCTSTR value)
{
    CMapStringToString mappedAudioFiles;
    GetMappedAudioFiles(mappedAudioFiles);
    mappedAudioFiles[key] = value;
    SetMappedAudioFiles(mappedAudioFiles);
}
