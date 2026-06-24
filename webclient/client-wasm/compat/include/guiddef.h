#pragma once
#include "tm_emscripten_prelude.h"

struct GUID {
  std::uint32_t Data1;
  std::uint16_t Data2;
  std::uint16_t Data3;
  std::uint8_t Data4[8];
};

using IID = GUID;
using CLSID = GUID;
using FMTID = GUID;

using REFGUID = const GUID&;
using REFIID = const IID&;
using REFCLSID = const CLSID&;

#ifndef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  inline constexpr GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#endif

#ifndef GUID_NULL
inline constexpr GUID GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
#endif
