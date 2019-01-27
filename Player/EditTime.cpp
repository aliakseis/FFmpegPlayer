// EditTime.cpp : implementation file
//

#include "stdafx.h"
#include "EditTime.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CEditTime

CEditTime::CEditTime()
{
}

CEditTime::~CEditTime()
{
}


BEGIN_MESSAGE_MAP(CEditTime, CEdit)
	//{{AFX_MSG_MAP(CEditTime)
	ON_WM_CHAR()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

BOOL CEditTime::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	if(CEdit::Create(dwStyle, rect, pParentWnd, nID))
	{
		SetLimitText(6);
		SetWindowText(_T("0.00"));
		return TRUE;
	}
	return FALSE;
}

/////////////////////////////////////////////////////////////////////////////
// CEditTime message handlers

void CEditTime::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
	if(nChar >= 32 && (nChar < '0' || nChar > '9'))
		if('.' == nChar)
		{
			CString strBuf;
			GetWindowText(strBuf);
			if(-1 != strBuf.Find(_T('.')))
				return;
		}
		else
			return;
	CEdit::OnChar(nChar, nRepCnt, nFlags);
}

void CEditTime::Reset()
{
	SetWindowText(_T(""));
}

void CEditTime::SetValue(double fTime)
{
	CString strBuf;
	strBuf.Format(_T("%.2f"), fTime);
	SetWindowText(strBuf);
}

double CEditTime::GetValue() const
{
	CString strBuf;
	GetWindowText(strBuf);
	return _ttof(strBuf);
}

bool CEditTime::IsEmpty() const
{
    CString strBuf;
    GetWindowText(strBuf);
    return strBuf.IsEmpty();
}
