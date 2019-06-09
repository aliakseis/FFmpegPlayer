
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

#include <boost/log/sinks/debug_output_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <boost/log/expressions.hpp>

#include <boost/log/trivial.hpp>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//#define USE_DIRECT2D_VIEW

namespace {

void init_logging()
{
    namespace expr = boost::log::expressions;

    boost::log::add_common_attributes();

    auto core = boost::log::core::get();

    // Create the sink. The backend requires synchronization in the frontend.
    auto sink(boost::make_shared<boost::log::sinks::synchronous_sink<boost::log::sinks::debug_output_backend>>());

    sink->set_formatter(expr::stream
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
    CSingleDocTemplate* pDocTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME,
        RUNTIME_CLASS(CPlayerDoc),
        RUNTIME_CLASS(CMainFrame),       // main SDI frame window
#ifdef USE_DIRECT2D_VIEW
        RUNTIME_CLASS(CPlayerViewD2D));
#else
        RUNTIME_CLASS(CPlayerView));
#endif
    if (!pDocTemplate)
        return FALSE;
    AddDocTemplate(pDocTemplate);

    // Dispatch commands specified on the command line.  Will return FALSE if
    // app was launched with /RegServer, /Register, /Unregserver or /Unregister.
    if (!ProcessShellCommand(cmdInfo))
        return FALSE;

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

// Dialog Data
    enum { IDD = IDD_ABOUTBOX };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// App command to run the dialog
void CPlayerApp::OnAppAbout()
{
    CAboutDlg aboutDlg;
    aboutDlg.DoModal();
}

void CPlayerApp::OnAsyncUrl(WPARAM wParam, LPARAM)
{
    CComBSTR url;
    url.Attach((BSTR)wParam);
    POSITION pos1 = GetFirstDocTemplatePosition();
    if (CDocTemplate* templ = GetNextDocTemplate(pos1))
    {
        POSITION pos2 = templ->GetFirstDocPosition();
        if (CPlayerDoc* doc = dynamic_cast<CPlayerDoc*>(templ->GetNextDoc(pos2)))
        {
            doc->OnAsyncUrl(CString(url));
        }
    }
}

// CPlayerApp message handlers



