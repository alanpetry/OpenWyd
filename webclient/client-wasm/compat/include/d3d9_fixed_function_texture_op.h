#pragma once

#include "d3d9.h"

// D3D9 texture-stage states that are not yet part of every local shim header.
// Keep these as helper-local constants so the runtime can consume real D3D9
// slots without sprinkling numeric literals through the WebGL fixed-function path.
constexpr D3DTEXTURESTAGESTATETYPE kD3D9TextureStageColorArg0 =
    static_cast<D3DTEXTURESTAGESTATETYPE>(26u);
constexpr D3DTEXTURESTAGESTATETYPE kD3D9TextureStageAlphaArg0 =
    static_cast<D3DTEXTURESTAGESTATETYPE>(27u);
constexpr D3DTEXTURESTAGESTATETYPE kD3D9TextureStageResultArg =
    static_cast<D3DTEXTURESTAGESTATETYPE>(28u);
constexpr D3DTEXTURESTAGESTATETYPE kD3D9TextureStageConstant =
    static_cast<D3DTEXTURESTAGESTATETYPE>(32u);

constexpr DWORD kD3D9TextureArgTemp = 0x00000005u;
constexpr DWORD kD3D9TextureArgConstant = 0x00000006u;
constexpr DWORD kD3D9TextureArgSelectorMask = 0x0000000Fu;

constexpr bool D3D9TextureOpUsesArg0(DWORD op) {
  return op == D3DTOP_MULTIPLYADD || op == D3DTOP_LERP;
}

constexpr DWORD D3D9TextureOpDefaultArg0(DWORD op) {
  return D3D9TextureOpUsesArg0(op) ? D3DTA_CURRENT : D3DTA_CURRENT;
}

constexpr DWORD D3D9TextureOpResolvedArg0(DWORD op, DWORD configured_arg0) {
  return D3D9TextureOpUsesArg0(op) ? configured_arg0 : D3D9TextureOpDefaultArg0(op);
}

constexpr bool D3D9TextureArgSelectsTemp(DWORD arg) {
  return (arg & kD3D9TextureArgSelectorMask) == kD3D9TextureArgTemp;
}

constexpr bool D3D9TextureArgSelectsConstant(DWORD arg) {
  return (arg & kD3D9TextureArgSelectorMask) == kD3D9TextureArgConstant;
}

constexpr bool D3D9TextureStageWritesTemp(DWORD result_arg) {
  return D3D9TextureArgSelectsTemp(result_arg);
}
