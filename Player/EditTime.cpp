// EditTime.cpp : implementation file
//

#include "stdafx.h"
#include "EditTime.h"

#include <boost/regex.hpp>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace {

void EscapeString(CString &str)
{
    int iIndex = 0;

    while (iIndex < str.GetLength())
    {
        if (_tcschr(_T(".|*?+(){}[]^$\\"), str[iIndex]))
        {
            str.Insert(iIndex++, _T("\\"));
        }

        ++iIndex;
    }
}

bool Match(const CString& strText)
{
    if (strText.IsEmpty())
        return true;

    const unsigned int uiWhole = 19;
    TCHAR szSeparator[4] = _T("");
    CString strSeparator;
    CString strSignedRegEx;

    ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, szSeparator, 4);
    strSeparator = szSeparator;
    EscapeString(strSeparator);
    strSignedRegEx.Format(_T("[-+]?([0-9]{1,%d}(%s[0-9]{0,2})?")
        _T("|%s[0-9]{1,2})"), uiWhole, strSeparator, strSeparator);

#ifdef _UNICODE
    typedef boost::wregex CRegEx;
#else
    typedef boost::regex CRegEx;
#endif

    CRegEx signedRegEx;
    signedRegEx.assign(strSignedRegEx);

    boost::match_results<const TCHAR*> what;

    return boost::regex_match(static_cast<const TCHAR*>(strText),
        what, signedRegEx, boost::match_default | boost::match_partial);
}

} // namespace


enum { WM_RESET = WM_USER + 101 };

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
    ON_MESSAGE(WM_RESET, &CEditTime::OnReset)
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CEditTime message handlers

void CEditTime::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
    if (nChar >= 32)
    {
        int iStartIndex = -1;
        int iEndIndex = -1;

        GetSel(iStartIndex, iEndIndex);
        CString strProposedText;
        GetWindowText(strProposedText);
        strProposedText.Delete(iStartIndex, iEndIndex - iStartIndex);
        strProposedText.Insert(iStartIndex, static_cast<TCHAR>(nChar));

        if (!Match(strProposedText))
            return;
    }
    CEdit::OnChar(nChar, nRepCnt, nFlags);
}

void CEditTime::Reset()
{
    SendNotifyMessage(WM_RESET, 0, 0);
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

LRESULT CEditTime::OnReset(WPARAM, LPARAM)
{
    SetWindowText(_T(""));
    return 0;
}
