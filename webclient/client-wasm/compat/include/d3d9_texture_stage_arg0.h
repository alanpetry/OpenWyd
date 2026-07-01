#pragma once

#include "d3d9.h"

// D3DTOP_MULTIPLYADD and D3DTOP_LERP consume ARG0 in real D3D9 texture-stage
// state. Keep these symbols typed without expanding the core enum in d3d9.h.
static constexpr D3DTEXTURESTAGESTATETYPE D3DTSS_COLORARG0 =
    static_cast<D3DTEXTURESTAGESTATETYPE>(26u);
static constexpr D3DTEXTURESTAGESTATETYPE D3DTSS_ALPHAARG0 =
    static_cast<D3DTEXTURESTAGESTATETYPE>(27u);
static constexpr D3DTEXTURESTAGESTATETYPE D3DTSS_RESULTARG =
    static_cast<D3DTEXTURESTAGESTATETYPE>(28u);

#ifndef D3DTA_TEMP
#define D3DTA_TEMP 0x00000005
#endif
#ifndef D3DTA_CONSTANT
#define D3DTA_CONSTANT 0x00000006
#endif

static constexpr DWORD kD3D9TextureStageConstantRegisterIndex = 32u;

inline bool D3D9TextureStageStateUsesArg0(D3DTEXTURESTAGESTATETYPE type) {
  return type == D3DTSS_COLORARG0 || type == D3DTSS_ALPHAARG0;
}

inline DWORD D3D9TextureStageArgSelector(DWORD arg) {
  return arg & ~(static_cast<DWORD>(D3DTA_COMPLEMENT) |
                 static_cast<DWORD>(D3DTA_ALPHAREPLICATE));
}

inline bool D3D9TextureStageArgUsesTemp(DWORD arg) {
  return D3D9TextureStageArgSelector(arg) == static_cast<DWORD>(D3DTA_TEMP);
}

inline bool D3D9TextureStageArgUsesConstant(DWORD arg) {
  return D3D9TextureStageArgSelector(arg) == static_cast<DWORD>(D3DTA_CONSTANT);
}
