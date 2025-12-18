#pragma once
#include "afxdialogex.h"


// CDialogVideoFilter dialog

class CDialogVideoFilter : public CDialog
{
	DECLARE_DYNAMIC(CDialogVideoFilter)

public:
	CDialogVideoFilter(CWnd* pParent = nullptr);   // standard constructor
	virtual ~CDialogVideoFilter();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG_VIDEO_FILTER };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
    CString m_videoFilter;
};
