
// Player.h : main header file for the Player application
//
#pragma once

#ifndef __AFXWIN_H__
    #error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"       // main symbols

class CPlayerDoc;

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

    CString GetMappedAudioFile(LPCTSTR key);
    void SetMappedAudioFile(LPCTSTR key, LPCTSTR value);

// Implementation
    afx_msg void OnAppAbout();
    afx_msg void OnAsyncUrl(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

    CPlayerDoc* GetPlayerDocument();

private:
    bool GetMappedAudioFiles(CMapStringToString& map);
    void SetMappedAudioFiles(CMapStringToString& map);

    void HandleMruList();
};

extern CPlayerApp theApp;
