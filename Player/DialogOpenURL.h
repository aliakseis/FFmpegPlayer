#pragma once


// CDialogOpenURL dialog

class CDialogOpenURL : public CDialog
{
	DECLARE_DYNAMIC(CDialogOpenURL)

public:
	explicit CDialogOpenURL(CWnd* pParent = NULL);   // standard constructor
	virtual ~CDialogOpenURL();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG_OPEN_URL };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
    CString m_URL;
	int m_bParse{};
};
