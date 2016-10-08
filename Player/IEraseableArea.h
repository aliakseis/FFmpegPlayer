#pragma once

class CWnd;
class CDC;

class IEraseableArea
{
public:
    virtual void OnErase(CWnd* pInitiator, CDC* pDC) = 0;
};
