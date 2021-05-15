#pragma once

/////////////////////////////////////////////////////////////////////////////
// CEditTime window

class CEditTime : public CEdit
{
// Construction
public:
	CEditTime();

// Attributes
public:
	void Reset();
	void SetValue(double fTime);
	double GetValue() const;
    bool IsEmpty() const;

// Operations

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CEditTime)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CEditTime();

	// Generated message map functions
protected:
    //{{AFX_MSG(CEditTime)
    afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg LRESULT OnReset(WPARAM wParam, LPARAM lParam);
    //}}AFX_MSG

    DECLARE_MESSAGE_MAP()

private:
	CFont m_Font;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

