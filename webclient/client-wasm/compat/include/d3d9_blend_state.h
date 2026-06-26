#pragma once

#include "d3d9.h"

// Shared D3D9 blend-state mapping for the WASM/WebGL compatibility path.
// Keep this header free of GL includes so it can be used by state tests and by
// win32_emscripten_stubs.cpp without forcing GLES types into d3d9.h.
namespace wyd::d3d9_compat {

enum class WebGLBlendFactor : DWORD {
  Zero = 0,
  One = 1,
  SrcColor = 0x0300,
  OneMinusSrcColor = 0x0301,
  SrcAlpha = 0x0302,
  OneMinusSrcAlpha = 0x0303,
  DstAlpha = 0x0304,
  OneMinusDstAlpha = 0x0305,
  DstColor = 0x0306,
  OneMinusDstColor = 0x0307,
  SrcAlphaSaturate = 0x0308,
  ConstantColor = 0x8001,
  OneMinusConstantColor = 0x8002,
};

enum class WebGLBlendEquation : DWORD {
  Add = 0x8006,
  Subtract = 0x800A,
  ReverseSubtract = 0x800B,
  Min = 0x8007,
  Max = 0x8008,
};

inline WebGLBlendFactor BlendFactorToWebGL(DWORD blend) {
  switch (blend) {
    case D3DBLEND_ZERO:
      return WebGLBlendFactor::Zero;
    case D3DBLEND_ONE:
      return WebGLBlendFactor::One;
    case D3DBLEND_SRCCOLOR:
      return WebGLBlendFactor::SrcColor;
    case D3DBLEND_INVSRCCOLOR:
      return WebGLBlendFactor::OneMinusSrcColor;
    case D3DBLEND_SRCALPHA:
      return WebGLBlendFactor::SrcAlpha;
    case D3DBLEND_INVSRCALPHA:
      return WebGLBlendFactor::OneMinusSrcAlpha;
    case D3DBLEND_DESTALPHA:
      return WebGLBlendFactor::DstAlpha;
    case D3DBLEND_INVDESTALPHA:
      return WebGLBlendFactor::OneMinusDstAlpha;
    case D3DBLEND_DESTCOLOR:
      return WebGLBlendFactor::DstColor;
    case D3DBLEND_INVDESTCOLOR:
      return WebGLBlendFactor::OneMinusDstColor;
    case D3DBLEND_SRCALPHASAT:
      return WebGLBlendFactor::SrcAlphaSaturate;
    case D3DBLEND_BLENDFACTOR:
      return WebGLBlendFactor::ConstantColor;
    case D3DBLEND_INVBLENDFACTOR:
      return WebGLBlendFactor::OneMinusConstantColor;
    case D3DBLEND_BOTHSRCALPHA:
    case D3DBLEND_BOTHINVSRCALPHA:
    case D3DBLEND_SRCCOLOR2:
    case D3DBLEND_INVSRCCOLOR2:
    default:
      return WebGLBlendFactor::One;
  }
}

inline WebGLBlendEquation BlendOpToWebGL(DWORD op) {
  switch (op) {
    case D3DBLENDOP_SUBTRACT:
      return WebGLBlendEquation::Subtract;
    case D3DBLENDOP_REVSUBTRACT:
      return WebGLBlendEquation::ReverseSubtract;
    case D3DBLENDOP_MIN:
      return WebGLBlendEquation::Min;
    case D3DBLENDOP_MAX:
      return WebGLBlendEquation::Max;
    case D3DBLENDOP_ADD:
    default:
      return WebGLBlendEquation::Add;
  }
}

}  // namespace wyd::d3d9_compat
