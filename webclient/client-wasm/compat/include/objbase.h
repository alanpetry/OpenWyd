#pragma once
#include "unknwn.h"

#ifndef CLSCTX_INPROC_SERVER
#define CLSCTX_INPROC_SERVER 0x1
#endif
#ifndef CLSCTX_INPROC_HANDLER
#define CLSCTX_INPROC_HANDLER 0x2
#endif

#ifdef __cplusplus
extern "C" {
#endif

HRESULT CoInitialize(LPVOID pvReserved);
void CoUninitialize(void);
HRESULT CoCreateInstance(REFCLSID rclsid, IUnknown* pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv);

#ifdef __cplusplus
}
#endif
