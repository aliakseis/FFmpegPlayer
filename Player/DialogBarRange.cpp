// DialogBarRange.cpp : implementation file
//

#include "stdafx.h"
#include "Player.h"
#include "DialogBarRange.h"

#include "PlayerDoc.h"

#include "MakeDelegate.h"


// CDialogBarRange

IMPLEMENT_DYNAMIC(CDialogBarRange, CPaneDialog)

CDialogBarRange::CDialogBarRange()
: m_pDoc(nullptr)
{
    CDialogTemplate dlgtemplate;
    if (dlgtemplate.Load(MAKEINTRESOURCE(IDD)))
    {
        CSize size;
        dlgtemplate.GetSizeInPixels(&size);
        SetMinSize(size);
    }
}

CDialogBarRange::~CDialogBarRange()
{
    onDocDetaching();
}

void CDialogBarRange::onDocDetaching()
{
    if (m_pDoc)
    {
        m_pDoc->totalTimeUpdated.disconnect(MAKE_DELEGATE(&CDialogBarRange::onTotalTimeUpdated, this));
        m_pDoc->onDestructing.disconnect(MAKE_DELEGATE(&CDialogBarRange::onDocDetaching, this));

        m_pDoc = nullptr;
    }
}

void CDialogBarRange::setDocument(CPlayerDoc* pDoc)
{
    ASSERT(!m_pDoc);
    m_pDoc = pDoc;

    m_pDoc->totalTimeUpdated.connect(MAKE_DELEGATE(&CDialogBarRange::onTotalTimeUpdated, this));
    m_pDoc->onDestructing.connect(MAKE_DELEGATE(&CDialogBarRange::onDocDetaching, this));
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
    ON_UPDATE_COMMAND_UI(IDC_START, &CDialogBarRange::OnUpdateStart)
    ON_UPDATE_COMMAND_UI(IDC_START_RESET, &CDialogBarRange::OnUpdateStartReset)
    ON_UPDATE_COMMAND_UI(IDC_END, &CDialogBarRange::OnUpdateEnd)
    ON_UPDATE_COMMAND_UI(IDC_END_RESET, &CDialogBarRange::OnUpdateEndReset)
    ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_COPY_AS, &CDialogBarRange::OnUpdateSave)
    ON_EN_CHANGE(IDC_EDIT_START, &CDialogBarRange::OnChangeStart)
    ON_EN_CHANGE(IDC_EDIT_END, &CDialogBarRange::OnChangeEnd)
    ON_BN_CLICKED(IDC_LOSSLESS_CUT, &CDialogBarRange::OnBnClickedLosslessCut)
END_MESSAGE_MAP()



void CDialogBarRange::onTotalTimeUpdated(double)
{
	m_startTime.Reset();
	m_endTime.Reset();
}


// CDialogBarRange message handlers



void CDialogBarRange::OnStart()
{
    const double currentTime = m_pDoc->getCurrentTime();
    m_pDoc->setRangeStartTime(currentTime);
    m_startTime.SetValue(currentTime);
}


void CDialogBarRange::OnStartReset()
{
	m_pDoc->setRangeStartTime(m_pDoc->getStartTime());
	m_startTime.Reset();
}


void CDialogBarRange::OnEnd()
{
    const double currentTime = m_pDoc->getCurrentTime();
    m_pDoc->setRangeEndTime(currentTime);
    m_endTime.SetValue(currentTime);
}


void CDialogBarRange::OnEndReset()
{
	m_pDoc->setRangeEndTime(m_pDoc->getEndTime());
	m_endTime.Reset();
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

void CDialogBarRange::OnChangeStart()
{
    if (!m_startTime.IsEmpty())
    {
        m_pDoc->setRangeStartTime(m_startTime.GetValue());
    }
}

void CDialogBarRange::OnChangeEnd()
{
    if (!m_endTime.IsEmpty())
    {
        m_pDoc->setRangeEndTime(m_endTime.GetValue());
    }
}


void CDialogBarRange::OnBnClickedLosslessCut()
{
    if (auto control = static_cast<CButton*>(GetDlgItem(IDC_LOSSLESS_CUT)))
    {
        m_pDoc->setLosslessCut(control->GetCheck() != BST_UNCHECKED);
    }
}
