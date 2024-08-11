#pragma once

#include <atlcomcli.h>
#include <string>

// modifies url if it is a redirect
long HttpGetStatus(std::string& url);

CComVariant HttpGet(const char* url);
