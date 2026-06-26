#pragma once

#include "d3d9.h"

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

struct WebGLBlendState {
  WebGLBlendFactor src_rgb = WebGLBlendFactor::SrcAlpha;
  WebGLBlendFactor dst_rgb = WebGLBlendFactor::OneMinusSrcAlpha;
  WebGLBlendFactor src_alpha = WebGLBlendFactor::SrcAlpha;
  WebGLBlendFactor dst_alpha = WebGLBlendFactor::OneMinusSrcAlpha;
  WebGLBlendEquation rgb_op = WebGLBlendEquation::Add;
  WebGLBlendEquation alpha_op = WebGLBlendEquation::Add;
  DWORD blend_factor = 0xFFFFFFFFu;
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
    case D3DBLEND_BOTHSRCALPHA:
      return WebGLBlendFactor::SrcAlpha;
    case D3DBLEND_INVSRCALPHA:
    case D3DBLEND_BOTHINVSRCALPHA:
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

inline bool BlendFactorUsesConstantColor(WebGLBlendFactor factor) {
  return factor == WebGLBlendFactor::ConstantColor ||
         factor == WebGLBlendFactor::OneMinusConstantColor;
}

inline bool BlendStateUsesConstantColor(const WebGLBlendState& state) {
  return BlendFactorUsesConstantColor(state.src_rgb) ||
         BlendFactorUsesConstantColor(state.dst_rgb) ||
         BlendFactorUsesConstantColor(state.src_alpha) ||
         BlendFactorUsesConstantColor(state.dst_alpha);
}

inline WebGLBlendState BuildWebGLBlendState(DWORD src_blend,
                                            DWORD dst_blend,
                                            DWORD blend_op,
                                            DWORD blend_factor,
                                            bool separate_alpha_blend_enable,
                                            DWORD src_blend_alpha,
                                            DWORD dst_blend_alpha,
                                            DWORD blend_op_alpha) {
  WebGLBlendState state;
  state.src_rgb = BlendFactorToWebGL(src_blend);
  state.dst_rgb = BlendFactorToWebGL(dst_blend);
  state.rgb_op = BlendOpToWebGL(blend_op);
  state.blend_factor = blend_factor;

  if (separate_alpha_blend_enable) {
    state.src_alpha = BlendFactorToWebGL(src_blend_alpha);
    state.dst_alpha = BlendFactorToWebGL(dst_blend_alpha);
    state.alpha_op = BlendOpToWebGL(blend_op_alpha);
  } else {
    state.src_alpha = state.src_rgb;
    state.dst_alpha = state.dst_rgb;
    state.alpha_op = state.rgb_op;
  }

  return state;
}

}  // namespace wyd::d3d9_compat
