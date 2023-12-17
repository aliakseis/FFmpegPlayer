// EditTime.cpp : implementation file
//

#include "stdafx.h"
#include "EditTime.h"

#include <cmath>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace {


enum
{
    TIME_INVALID = -1000000000
};

int ReadUint(const TCHAR*& s)
{
    int buf = 0;
    for (; *s >= '0' && *s <= '9'; ++s) 
        buf = buf * 10 + *s - '0';

    return buf;
}

double ParseTime(const TCHAR* timeStr)
{
    int seconds = 0;

    int numDelims = 0;

    const bool minus = *timeStr == '-';
    if (minus)
        ++timeStr;

    for (;;)
    {
        switch (*timeStr)
        {
        case '\0':
            return minus ? -seconds : seconds;
        case ':':
            if (++numDelims > 2)
                return TIME_INVALID;
            seconds *= 60;
            ++timeStr;
            break;
        case '.':
        {
            ++timeStr;
            const auto prevPtr = timeStr;
            const auto millis = ReadUint(timeStr);
            if (*timeStr != '\0')
                return TIME_INVALID;
            const int msecStringLen = timeStr - prevPtr;
            if (msecStringLen > 3)
                return TIME_INVALID;
            if (msecStringLen == 0)
                return minus ? -seconds : seconds;
            const auto absResult = seconds + millis / std::pow(10, msecStringLen);
            return minus ? -absResult : absResult;
        }
        break;
        default:
            const auto prevPtr = timeStr;
            seconds += ReadUint(timeStr);
            if (prevPtr == timeStr)
                return TIME_INVALID;
        }
    }
}

bool Match(const TCHAR* timeStr)
{ 
    return ParseTime(timeStr) != TIME_INVALID; 
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
    return ParseTime(strBuf);
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
