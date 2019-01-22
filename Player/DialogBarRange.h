#pragma once

#include "EditTime.h"

class CPlayerDoc;

// CDialogBarRange

class CDialogBarRange : public CPaneDialog
{
	DECLARE_DYNAMIC(CDialogBarRange)

public:
	CDialogBarRange();
	virtual ~CDialogBarRange();

    enum { IDD = IDD_DIALOGBAR_RANGE };

    void setDocument(CPlayerDoc* pDoc);

    // Overrides
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CDialogBarPlayerControl)
protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    //}}AFX_VIRTUAL

protected:
    int GetCaptionHeight() const override { return 0; }

	void onTotalTimeUpdated(double secs);

protected:
	DECLARE_MESSAGE_MAP()
private:
    CPlayerDoc* m_pDoc;
public:
    CEditTime m_startTime;
    CEditTime m_endTime;
    afx_msg void OnStart();
    afx_msg void OnStartReset();
    afx_msg void OnEnd();
    afx_msg void OnEndReset();
    afx_msg void OnUpdateStart(CCmdUI *pCmdUI);
    afx_msg void OnUpdateStartReset(CCmdUI *pCmdUI);
    afx_msg void OnUpdateEnd(CCmdUI *pCmdUI);
    afx_msg void OnUpdateEndReset(CCmdUI *pCmdUI);
    afx_msg void OnUpdateSave(CCmdUI *pCmdUI);
};
