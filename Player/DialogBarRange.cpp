// DialogBarRange.cpp : implementation file
//

#include "stdafx.h"
#include "Player.h"
#include "DialogBarRange.h"

#include "PlayerDoc.h"

namespace {

CSize CalcDialogSize(UINT nResourceId) 
{ 
    CSize size;
    HRSRC hRsrc = ::FindResource(AfxGetApp()->m_hInstance, MAKEINTRESOURCE(nResourceId), RT_DIALOG);
    ASSERT(hRsrc != NULL); 
    HGLOBAL hTemplate = ::LoadResource(AfxGetApp()->m_hInstance, hRsrc);
    ASSERT(hTemplate != NULL);

    CDialogTemplate dlgtemplate(hTemplate);
    dlgtemplate.GetSizeInPixels(&size); 

    return size;
}

} // namespace

// CDialogBarRange

IMPLEMENT_DYNAMIC(CDialogBarRange, CPaneDialog)

CDialogBarRange::CDialogBarRange()
{
    SetMinSize(CalcDialogSize(IDD));
}

CDialogBarRange::~CDialogBarRange()
{
}

void CDialogBarRange::setDocument(CPlayerDoc* pDoc)
{
    ASSERT(!m_pDoc);
    m_pDoc = pDoc;
}

void CDialogBarRange::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CDialogBarRange)
    // NOTE: the ClassWizard will add DDX and DDV calls here
    //}}AFX_DATA_MAP
    DDX_Control(pDX, IDC_EDIT_START, m_startTime);
    DDX_Control(pDX, IDC_EDIT_END, m_endTime);
}

BEGIN_MESSAGE_MAP(CDialogBarRange, CPaneDialog)
    ON_COMMAND(IDC_START, &CDialogBarRange::OnStart)
    ON_COMMAND(IDC_START_RESET, &CDialogBarRange::OnStartReset)
    ON_COMMAND(IDC_END, &CDialogBarRange::OnEnd)
    ON_COMMAND(IDC_END_RESET, &CDialogBarRange::OnEndReset)
    ON_COMMAND(IDC_SAVE, &CDialogBarRange::OnSave)
    ON_UPDATE_COMMAND_UI(IDC_START, &CDialogBarRange::OnUpdateStart)
    ON_UPDATE_COMMAND_UI(IDC_START_RESET, &CDialogBarRange::OnUpdateStartReset)
    ON_UPDATE_COMMAND_UI(IDC_END, &CDialogBarRange::OnUpdateEnd)
    ON_UPDATE_COMMAND_UI(IDC_END_RESET, &CDialogBarRange::OnUpdateEndReset)
    ON_UPDATE_COMMAND_UI(IDC_SAVE, &CDialogBarRange::OnUpdateSave)
END_MESSAGE_MAP()



// CDialogBarRange message handlers




void CDialogBarRange::OnStart()
{
    const double currentTime = m_pDoc->getCurrentTime();
    m_pDoc->setRangeStartTime(currentTime);
    m_startTime.SetValue(currentTime);
}


void CDialogBarRange::OnStartReset()
{
    // TODO: Add your command handler code here
}


void CDialogBarRange::OnEnd()
{
    const double currentTime = m_pDoc->getCurrentTime();
    m_pDoc->setRangeEndTime(currentTime);
    m_endTime.SetValue(currentTime);
}


void CDialogBarRange::OnEndReset()
{
    // TODO: Add your command handler code here
}


void CDialogBarRange::OnSave()
{
    // TODO: Add your command handler code here
}


void CDialogBarRange::OnUpdateStart(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(m_pDoc->isPlaying());
}


void CDialogBarRange::OnUpdateStartReset(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(m_pDoc->isPlaying());
}


void CDialogBarRange::OnUpdateEnd(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(m_pDoc->isPlaying());
}


void CDialogBarRange::OnUpdateEndReset(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(m_pDoc->isPlaying());
}


void CDialogBarRange::OnUpdateSave(CCmdUI *pCmdUI)
{
    pCmdUI->Enable(m_pDoc->isPlaying());
}
