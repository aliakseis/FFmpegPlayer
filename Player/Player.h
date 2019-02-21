
// Player.h : main header file for the Player application
//
#pragma once

#ifndef __AFXWIN_H__
    #error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"       // main symbols


// CPlayerApp:
// See Player.cpp for the implementation of this class
//

class CPlayerApp : public CWinAppEx
{
public:
    CPlayerApp();


// Overrides
public:
    BOOL InitInstance() override;

// Implementation
    afx_msg void OnAppAbout();
    DECLARE_MESSAGE_MAP()
};

extern CPlayerApp theApp;
