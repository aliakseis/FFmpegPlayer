#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.

// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.

#include <WinSDKVer.h>

#define _WIN32_WINNT  _WIN32_WINNT_WIN8
#define NTDDI_VERSION NTDDI_WIN8
#define WINVER        _WIN32_WINNT_WIN8
#define _WIN32_IE     _WIN32_IE_WIN8

#include <SDKDDKVer.h>
