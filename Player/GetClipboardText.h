#pragma once

#include <string>

inline std::string GetClipboardText()
{
    std::string text;

    // Try opening the clipboard
    if (OpenClipboard(nullptr))
    {
        // Get handle of clipboard object for ANSI text
        if (HANDLE hData = GetClipboardData(CF_TEXT))
        {
            // Lock the handle to get the actual text pointer
            if (const char* pszText = static_cast<const char*>(GlobalLock(hData)))
            {
                // Save text in a string class instance
                text = pszText;
            }
            // Release the lock
            GlobalUnlock(hData);
        }

        // Release the clipboard
        CloseClipboard();
    }

    return text;
}
