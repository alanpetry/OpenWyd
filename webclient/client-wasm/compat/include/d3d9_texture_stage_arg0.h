#pragma once

#include "d3d9.h"

static constexpr DWORD kD3D9TextureStageConstantRegisterIndex =
    static_cast<DWORD>(D3DTSS_CONSTANT);

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