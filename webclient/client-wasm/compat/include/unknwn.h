#pragma once
#include "windows.h"

struct IUnknown {
  virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;
};
