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
  ConstantAlpha = 0x8003,
  OneMinusConstantAlpha = 0x8004,
};

enum class WebGLBlendEquation : DWORD {
  Add = 0x8006,
  Subtract = 0x800A,
  ReverseSubtract = 0x800B,
  Min = 0x8007,
  Max = 0x8008,
};

struct WebGLBlendColor {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
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

struct D3D9BlendRenderState {
  bool alpha_blend_enable = false;
  DWORD src_blend = D3DBLEND_SRCALPHA;
  DWORD dst_blend = D3DBLEND_INVSRCALPHA;
  DWORD blend_op = D3DBLENDOP_ADD;
  DWORD blend_factor = 0xFFFFFFFFu;
  bool separate_alpha_blend_enable = false;
  DWORD src_blend_alpha = D3DBLEND_SRCALPHA;
  DWORD dst_blend_alpha = D3DBLEND_INVSRCALPHA;
  DWORD blend_op_alpha = D3DBLENDOP_ADD;
};

struct WebGLBlendApplyFns {
  void (*blend_color)(float r, float g, float b, float a) = nullptr;
  void (*blend_func)(DWORD src, DWORD dst) = nullptr;
  void (*blend_func_separate)(DWORD src_rgb, DWORD dst_rgb, DWORD src_alpha, DWORD dst_alpha) = nullptr;
  void (*blend_equation_separate)(DWORD rgb_op, DWORD alpha_op) = nullptr;
  bool supports_min_max_equations = true;
};

struct WebGLBlendRuntimeFns {
  void (*enable_blend)() = nullptr;
  void (*disable_blend)() = nullptr;
  WebGLBlendApplyFns apply;
};

struct WebGLLegacyBlendFuncRuntimeFns {
  void (*enable_blend)() = nullptr;
  void (*disable_blend)() = nullptr;
  void (*blend_func)(DWORD src, DWORD dst) = nullptr;
};

inline D3D9BlendRenderState D3D9SpriteOverlayBlendRenderState() {
  D3D9BlendRenderState blend_state;
  blend_state.alpha_blend_enable = true;
  blend_state.src_blend = D3DBLEND_SRCALPHA;
  blend_state.dst_blend = D3DBLEND_INVSRCALPHA;
  blend_state.blend_op = D3DBLENDOP_ADD;
  blend_state.blend_factor = 0xFFFFFFFFu;
  blend_state.separate_alpha_blend_enable = false;
  blend_state.src_blend_alpha = D3DBLEND_SRCALPHA;
  blend_state.dst_blend_alpha = D3DBLEND_INVSRCALPHA;
  blend_state.blend_op_alpha = D3DBLENDOP_ADD;
  return blend_state;
}

inline void SetD3D9SpriteOverlayBlendRenderState(D3D9BlendRenderState* blend_state) {
  if (!blend_state) return;
  *blend_state = D3D9SpriteOverlayBlendRenderState();
}

inline bool SetD3D9BlendRenderState(D3D9BlendRenderState* blend_state,
                                    D3DRENDERSTATETYPE render_state,
                                    DWORD value) {
  if (!blend_state) return false;
  switch (render_state) {
    case D3DRS_ALPHABLENDENABLE:
      blend_state->alpha_blend_enable = (value != 0u);
      return true;
    case D3DRS_SRCBLEND:
      blend_state->src_blend = value;
      return true;
    case D3DRS_DESTBLEND:
      blend_state->dst_blend = value;
      return true;
    case D3DRS_BLENDOP:
      blend_state->blend_op = value;
      return true;
    case D3DRS_BLENDFACTOR:
      blend_state->blend_factor = value;
      return true;
    case D3DRS_SEPARATEALPHABLENDENABLE:
      blend_state->separate_alpha_blend_enable = (value != 0u);
      return true;
    case D3DRS_SRCBLENDALPHA:
      blend_state->src_blend_alpha = value;
      return true;
    case D3DRS_DESTBLENDALPHA:
      blend_state->dst_blend_alpha = value;
      return true;
    case D3DRS_BLENDOPALPHA:
      blend_state->blend_op_alpha = value;
      return true;
    default:
      return false;
  }
}

inline bool GetD3D9BlendRenderState(const D3D9BlendRenderState& blend_state,
                                    D3DRENDERSTATETYPE render_state,
                                    DWORD* value) {
  if (!value) return false;
  switch (render_state) {
    case D3DRS_ALPHABLENDENABLE:
      *value = blend_state.alpha_blend_enable ? 1u : 0u;
      return true;
    case D3DRS_SRCBLEND:
      *value = blend_state.src_blend;
      return true;
    case D3DRS_DESTBLEND:
      *value = blend_state.dst_blend;
      return true;
    case D3DRS_BLENDOP:
      *value = blend_state.blend_op;
      return true;
    case D3DRS_BLENDFACTOR:
      *value = blend_state.blend_factor;
      return true;
    case D3DRS_SEPARATEALPHABLENDENABLE:
      *value = blend_state.separate_alpha_blend_enable ? 1u : 0u;
      return true;
    case D3DRS_SRCBLENDALPHA:
      *value = blend_state.src_blend_alpha;
      return true;
    case D3DRS_DESTBLENDALPHA:
      *value = blend_state.dst_blend_alpha;
      return true;
    case D3DRS_BLENDOPALPHA:
      *value = blend_state.blend_op_alpha;
      return true;
    default:
      return false;
  }
}

inline WebGLBlendFactor BlendFactorToWebGL(DWORD blend) {
  switch (blend) {
    case D3DBLEND_ZERO:
      return WebGLBlendFactor::Zero;
    case D3DBLEND_ONE:
      return WebGLBlendFactor::One;
    case D3DBLEND_SRCCOLOR:
    case D3DBLEND_SRCCOLOR2:
      return WebGLBlendFactor::SrcColor;
    case D3DBLEND_INVSRCCOLOR:
    case D3DBLEND_INVSRCCOLOR2:
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
    default:
      return WebGLBlendFactor::One;
  }
}

inline WebGLBlendFactor BlendAlphaFactorToWebGL(DWORD blend) {
  switch (blend) {
    case D3DBLEND_SRCCOLOR:
    case D3DBLEND_SRCALPHASAT:
    case D3DBLEND_SRCCOLOR2:
      return WebGLBlendFactor::SrcAlpha;
    case D3DBLEND_INVSRCCOLOR:
    case D3DBLEND_INVSRCCOLOR2:
      return WebGLBlendFactor::OneMinusSrcAlpha;
    case D3DBLEND_DESTCOLOR:
      return WebGLBlendFactor::DstAlpha;
    case D3DBLEND_INVDESTCOLOR:
      return WebGLBlendFactor::OneMinusDstAlpha;
    case D3DBLEND_BLENDFACTOR:
      return WebGLBlendFactor::ConstantAlpha;
    case D3DBLEND_INVBLENDFACTOR:
      return WebGLBlendFactor::OneMinusConstantAlpha;
    default:
      return BlendFactorToWebGL(blend);
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

inline WebGLBlendEquation BlendEquationForRuntime(
    WebGLBlendEquation equation,
    bool supports_min_max_equations) {
  if (supports_min_max_equations) return equation;
  switch (equation) {
    case WebGLBlendEquation::Min:
    case WebGLBlendEquation::Max:
      return WebGLBlendEquation::Add;
    default:
      return equation;
  }
}

inline void ApplyBothSourceAlphaBlendPair(DWORD src_blend,
                                          WebGLBlendFactor* src,
                                          WebGLBlendFactor* dst) {
  if (!src || !dst) return;
  switch (src_blend) {
    case D3DBLEND_BOTHSRCALPHA:
      *src = WebGLBlendFactor::SrcAlpha;
      *dst = WebGLBlendFactor::OneMinusSrcAlpha;
      break;
    case D3DBLEND_BOTHINVSRCALPHA:
      *src = WebGLBlendFactor::OneMinusSrcAlpha;
      *dst = WebGLBlendFactor::SrcAlpha;
      break;
    default:
      break;
  }
}

inline bool BlendFactorUsesConstantColor(WebGLBlendFactor factor) {
  return factor == WebGLBlendFactor::ConstantColor ||
         factor == WebGLBlendFactor::OneMinusConstantColor ||
         factor == WebGLBlendFactor::ConstantAlpha ||
         factor == WebGLBlendFactor::OneMinusConstantAlpha;
}

inline bool BlendStateUsesConstantColor(const WebGLBlendState& state) {
  return BlendFactorUsesConstantColor(state.src_rgb) ||
         BlendFactorUsesConstantColor(state.dst_rgb) ||
         BlendFactorUsesConstantColor(state.src_alpha) ||
         BlendFactorUsesConstantColor(state.dst_alpha);
}

inline float BlendFactorColorChannel(DWORD color, DWORD shift) {
  return static_cast<float>((color >> shift) & 0xFFu) / 255.0f;
}

inline WebGLBlendColor BlendFactorColorToWebGL(DWORD blend_factor) {
  WebGLBlendColor color;
  color.r = BlendFactorColorChannel(blend_factor, 16);
  color.g = BlendFactorColorChannel(blend_factor, 8);
  color.b = BlendFactorColorChannel(blend_factor, 0);
  color.a = BlendFactorColorChannel(blend_factor, 24);
  return color;
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
  ApplyBothSourceAlphaBlendPair(src_blend, &state.src_rgb, &state.dst_rgb);
  state.rgb_op = BlendOpToWebGL(blend_op);
  state.blend_factor = blend_factor;

  if (separate_alpha_blend_enable) {
    state.src_alpha = BlendAlphaFactorToWebGL(src_blend_alpha);
    state.dst_alpha = BlendAlphaFactorToWebGL(dst_blend_alpha);
    ApplyBothSourceAlphaBlendPair(src_blend_alpha, &state.src_alpha, &state.dst_alpha);
    state.alpha_op = BlendOpToWebGL(blend_op_alpha);
  } else {
    state.src_alpha = BlendAlphaFactorToWebGL(src_blend);
    state.dst_alpha = BlendAlphaFactorToWebGL(dst_blend);
    ApplyBothSourceAlphaBlendPair(src_blend, &state.src_alpha, &state.dst_alpha);
    state.alpha_op = state.rgb_op;
  }

  return state;
}

inline WebGLBlendState BuildWebGLBlendState(const D3D9BlendRenderState& blend_state) {
  return BuildWebGLBlendState(
      blend_state.src_blend,
      blend_state.dst_blend,
      blend_state.blend_op,
      blend_state.blend_factor,
      blend_state.separate_alpha_blend_enable,
      blend_state.src_blend_alpha,
      blend_state.dst_blend_alpha,
      blend_state.blend_op_alpha);
}

inline bool WebGLBlendStateFitsLegacyBlendFunc(const WebGLBlendState& state) {
  if (BlendStateUsesConstantColor(state)) return false;
  if (state.rgb_op != WebGLBlendEquation::Add || state.alpha_op != WebGLBlendEquation::Add) return false;
  return state.src_rgb == state.src_alpha && state.dst_rgb == state.dst_alpha;
}

inline bool D3D9BlendRenderStateFitsLegacyBlendFunc(
    const D3D9BlendRenderState& blend_state) {
  return WebGLBlendStateFitsLegacyBlendFunc(BuildWebGLBlendState(blend_state));
}

inline void ApplyWebGLBlendState(const WebGLBlendState& state,
                                 const WebGLBlendApplyFns& apply) {
  if (BlendStateUsesConstantColor(state) && apply.blend_color) {
    const WebGLBlendColor color = BlendFactorColorToWebGL(state.blend_factor);
    apply.blend_color(color.r, color.g, color.b, color.a);
  }

  if (apply.blend_func_separate) {
    apply.blend_func_separate(
        static_cast<DWORD>(state.src_rgb),
        static_cast<DWORD>(state.dst_rgb),
        static_cast<DWORD>(state.src_alpha),
        static_cast<DWORD>(state.dst_alpha));
  } else if (apply.blend_func) {
    apply.blend_func(
        static_cast<DWORD>(state.src_rgb),
        static_cast<DWORD>(state.dst_rgb));
  }

  if (apply.blend_equation_separate) {
    const WebGLBlendEquation rgb_op =
        BlendEquationForRuntime(state.rgb_op, apply.supports_min_max_equations);
    const WebGLBlendEquation alpha_op =
        BlendEquationForRuntime(state.alpha_op, apply.supports_min_max_equations);
    apply.blend_equation_separate(
        static_cast<DWORD>(rgb_op),
        static_cast<DWORD>(alpha_op));
  }
}

inline void ApplyD3D9BlendRenderState(const D3D9BlendRenderState& blend_state,
                                      const WebGLBlendRuntimeFns& runtime) {
  if (blend_state.alpha_blend_enable) {
    if (runtime.enable_blend) runtime.enable_blend();
  } else if (runtime.disable_blend) {
    runtime.disable_blend();
  }

  ApplyWebGLBlendState(BuildWebGLBlendState(blend_state), runtime.apply);
}

inline void ApplyD3D9BlendRenderStateLegacyFunc(
    const D3D9BlendRenderState& blend_state,
    const WebGLLegacyBlendFuncRuntimeFns& runtime) {
  WebGLBlendRuntimeFns full_runtime;
  full_runtime.enable_blend = runtime.enable_blend;
  full_runtime.disable_blend = runtime.disable_blend;
  full_runtime.apply.blend_func = runtime.blend_func;
  ApplyD3D9BlendRenderState(blend_state, full_runtime);
}

inline bool ApplyD3D9BlendRenderStateLegacyFuncIfExact(
    const D3D9BlendRenderState& blend_state,
    const WebGLLegacyBlendFuncRuntimeFns& runtime) {
  if (!D3D9BlendRenderStateFitsLegacyBlendFunc(blend_state)) return false;
  ApplyD3D9BlendRenderStateLegacyFunc(blend_state, runtime);
  return true;
}

inline bool ApplyD3D9BlendRenderStateLegacyFuncOrFull(
    const D3D9BlendRenderState& blend_state,
    const WebGLLegacyBlendFuncRuntimeFns& legacy_runtime,
    const WebGLBlendRuntimeFns& full_runtime) {
  if (ApplyD3D9BlendRenderStateLegacyFuncIfExact(blend_state, legacy_runtime)) {
    return true;
  }
  ApplyD3D9BlendRenderState(blend_state, full_runtime);
  return false;
}

}  // namespace wyd::d3d9_compat
