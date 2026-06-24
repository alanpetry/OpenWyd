#pragma once
#include "strmif.h"

struct IVideoWindow : public IUnknown {
  template <typename... Args>
  HRESULT NotifyOwnerMessage(Args...) { return E_NOTIMPL; }
};
struct IBasicVideo : public IUnknown {};
