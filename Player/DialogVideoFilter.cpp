// DialogVideoFilter.cpp : implementation file
//

#include "stdafx.h"
#include "Player.h"
#include "afxdialogex.h"
#include "DialogVideoFilter.h"


// CDialogVideoFilter dialog

IMPLEMENT_DYNAMIC(CDialogVideoFilter, CDialog)

CDialogVideoFilter::CDialogVideoFilter(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_DIALOG_VIDEO_FILTER, pParent)
    , m_videoFilter(_T(""))
    , m_enableVideoFilter(FALSE)
{

}

CDialogVideoFilter::~CDialogVideoFilter()
{
}

void CDialogVideoFilter::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_VIDEO_FILTER, m_videoFilter);
    DDX_Check(pDX, IDC_ENABLE_VIDEO_FILTER, m_enableVideoFilter);
}


BEGIN_MESSAGE_MAP(CDialogVideoFilter, CDialog)
END_MESSAGE_MAP()


// CDialogVideoFilter message handlers
