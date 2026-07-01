#pragma once

#include "d3d9.h"

static constexpr DWORD kD3D9TextureStageConstantRegisterIndex =
    static_cast<DWORD>(D3DTSS_CONSTANT);
static constexpr DWORD kD3D9TextureStageDefaultArg0 =
    static_cast<DWORD>(D3DTA_CURRENT);

inline bool D3D9TextureStageStateUsesArg0(D3DTEXTURESTAGESTATETYPE type) {
  return type == D3DTSS_COLORARG0 || type == D3DTSS_ALPHAARG0;
}

inline bool D3D9TextureStageOpUsesArg0(D3DTEXTUREOP op) {
  return op == D3DTOP_MULTIPLYADD || op == D3DTOP_LERP;
}

inline DWORD D3D9TextureStageResolvedArg0(D3DTEXTUREOP op, DWORD stored_arg0) {
  return D3D9TextureStageOpUsesArg0(op) ? stored_arg0 : kD3D9TextureStageDefaultArg0;
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
