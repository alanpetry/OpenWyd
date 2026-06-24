#pragma once
#include "windows.h"

using HINTERNET = void*;

#ifdef __cplusplus
extern "C" {
#endif

HINTERNET InternetOpenA(LPCSTR lpszAgent, DWORD dwAccessType, LPCSTR lpszProxy, LPCSTR lpszProxyBypass, DWORD dwFlags);
HINTERNET InternetOpenUrlA(HINTERNET hInternet, LPCSTR lpszUrl, LPCSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwFlags, DWORD_PTR dwContext);
BOOL InternetReadFile(HINTERNET hFile, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead);
BOOL InternetCloseHandle(HINTERNET hInternet);

#ifdef __cplusplus
}
#endif

#ifndef InternetOpen
#define InternetOpen InternetOpenA
#endif
#ifndef InternetOpenUrl
#define InternetOpenUrl InternetOpenUrlA
#endif
