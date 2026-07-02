#pragma once

#include "d3d9.h"

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#include <cstring>
#endif

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
  WebGLBlendFactor src_rgb = WebGLBlendFactor::One;
  WebGLBlendFactor dst_rgb = WebGLBlendFactor::Zero;
  WebGLBlendFactor src_alpha = WebGLBlendFactor::One;
  WebGLBlendFactor dst_alpha = WebGLBlendFactor::Zero;
  WebGLBlendEquation rgb_op = WebGLBlendEquation::Add;
  WebGLBlendEquation alpha_op = WebGLBlendEquation::Add;
  DWORD blend_factor = 0xFFFFFFFFu;
};

struct D3D9BlendRenderState {
  bool alpha_blend_enable = false;
  DWORD src_blend = D3DBLEND_ONE;
  DWORD dst_blend = D3DBLEND_ZERO;
  DWORD blend_op = D3DBLENDOP_ADD;
  DWORD blend_factor = 0xFFFFFFFFu;
  bool separate_alpha_blend_enable = false;
  DWORD src_blend_alpha = D3DBLEND_ONE;
  DWORD dst_blend_alpha = D3DBLEND_ZERO;
  DWORD blend_op_alpha = D3DBLENDOP_ADD;
};

inline D3D9BlendRenderState MakeD3D9BlendRenderState(
    bool alpha_blend_enable,
    DWORD src_blend,
    DWORD dst_blend) {
  D3D9BlendRenderState render_state;
  render_state.alpha_blend_enable = alpha_blend_enable;
  render_state.src_blend = src_blend;
  render_state.dst_blend = dst_blend;
  render_state.src_blend_alpha = src_blend;
  render_state.dst_blend_alpha = dst_blend;
  return render_state;
}

inline D3D9BlendRenderState MakeD3D9SpriteBlendRenderState() {
  D3D9BlendRenderState render_state;
  render_state.alpha_blend_enable = true;
  render_state.src_blend = D3DBLEND_SRCALPHA;
  render_state.dst_blend = D3DBLEND_INVSRCALPHA;
  render_state.blend_op = D3DBLENDOP_ADD;
  render_state.blend_factor = 0xFFFFFFFFu;
  render_state.separate_alpha_blend_enable = false;
  render_state.src_blend_alpha = D3DBLEND_SRCALPHA;
  render_state.dst_blend_alpha = D3DBLEND_INVSRCALPHA;
  render_state.blend_op_alpha = D3DBLENDOP_ADD;
  return render_state;
}

inline bool SetD3D9BlendRenderStateValue(D3D9BlendRenderState* render_state,
                                         D3DRENDERSTATETYPE state,
                                         DWORD value) {
  if (!render_state) return false;
  switch (state) {
    case D3DRS_ALPHABLENDENABLE:
      render_state->alpha_blend_enable = (value != 0u);
      return true;
    case D3DRS_SRCBLEND:
      render_state->src_blend = value;
      return true;
    case D3DRS_DESTBLEND:
      render_state->dst_blend = value;
      return true;
    case D3DRS_BLENDOP:
      render_state->blend_op = value;
      return true;
    case D3DRS_BLENDFACTOR:
      render_state->blend_factor = value;
      return true;
    case D3DRS_SEPARATEALPHABLENDENABLE:
      render_state->separate_alpha_blend_enable = (value != 0u);
      return true;
    case D3DRS_SRCBLENDALPHA:
      render_state->src_blend_alpha = value;
      return true;
    case D3DRS_DESTBLENDALPHA:
      render_state->dst_blend_alpha = value;
      return true;
    case D3DRS_BLENDOPALPHA:
      render_state->blend_op_alpha = value;
      return true;
    default:
      return false;
  }
}

inline bool SetD3D9BlendRenderStateValue(D3D9BlendRenderState& render_state,
                                         D3DRENDERSTATETYPE state,
                                         DWORD value) {
  return SetD3D9BlendRenderStateValue(&render_state, state, value);
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
    case D3DBLEND_SRCCOLOR2:
      return WebGLBlendFactor::SrcAlpha;
    case D3DBLEND_SRCALPHASAT:
      return WebGLBlendFactor::One;
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

inline void ApplyBothSourceAlphaBlendPair(DWORD src_blend,
                                          WebGLBlendFactor* src_factor,
                                          WebGLBlendFactor* dst_factor) {
  if (!src_factor || !dst_factor) return;
  switch (src_blend) {
    case D3DBLEND_BOTHSRCALPHA:
      *src_factor = WebGLBlendFactor::SrcAlpha;
      *dst_factor = WebGLBlendFactor::OneMinusSrcAlpha;
      break;
    case D3DBLEND_BOTHINVSRCALPHA:
      *src_factor = WebGLBlendFactor::OneMinusSrcAlpha;
      *dst_factor = WebGLBlendFactor::SrcAlpha;
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

inline WebGLBlendFactor NormalizeWebGLConstantBlendFactorFamily(
    WebGLBlendFactor factor) {
  switch (factor) {
    case WebGLBlendFactor::ConstantAlpha:
      return WebGLBlendFactor::ConstantColor;
    case WebGLBlendFactor::OneMinusConstantAlpha:
      return WebGLBlendFactor::OneMinusConstantColor;
    default:
      return factor;
  }
}

inline bool BlendStateUsesConstantColor(const WebGLBlendState& state) {
  return BlendFactorUsesConstantColor(state.src_rgb) ||
         BlendFactorUsesConstantColor(state.dst_rgb) ||
         BlendFactorUsesConstantColor(state.src_alpha) ||
         BlendFactorUsesConstantColor(state.dst_alpha);
}

inline bool BlendStateUsesSeparateFactors(const WebGLBlendState& state) {
  return state.src_rgb != state.src_alpha || state.dst_rgb != state.dst_alpha;
}

inline bool BlendStateUsesSeparateEquations(const WebGLBlendState& state) {
  return state.rgb_op != state.alpha_op;
}

inline bool BlendStateUsesSeparateWebGLCalls(const WebGLBlendState& state) {
  return BlendStateUsesSeparateFactors(state) || BlendStateUsesSeparateEquations(state);
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

#ifdef __EMSCRIPTEN__
inline GLenum WebGLEnum(WebGLBlendFactor factor) {
  return static_cast<GLenum>(static_cast<DWORD>(factor));
}

inline GLenum WebGLEnum(WebGLBlendEquation equation) {
  return static_cast<GLenum>(static_cast<DWORD>(equation));
}

inline bool WebGLStringContains(GLenum name, const char* needle) {
  if (!needle || needle[0] == '\0') return false;
  const auto* raw = reinterpret_cast<const char*>(glGetString(name));
  return raw && std::strstr(raw, needle) != nullptr;
}

inline bool WebGLMinMaxBlendEquationSupported() {
  return WebGLStringContains(GL_VERSION, "WebGL 2") ||
         WebGLStringContains(GL_VERSION, "OpenGL ES 3") ||
         WebGLStringContains(GL_EXTENSIONS, "EXT_blend_minmax");
}

inline WebGLBlendEquation SupportedWebGLBlendEquation(WebGLBlendEquation equation) {
  if (equation != WebGLBlendEquation::Min && equation != WebGLBlendEquation::Max) {
    return equation;
  }
  return WebGLMinMaxBlendEquationSupported() ? equation : WebGLBlendEquation::Add;
}

inline WebGLBlendFactor SupportedWebGLBlendFactor(
    WebGLBlendFactor factor,
    bool source_rgb_factor) {
  if (factor != WebGLBlendFactor::SrcAlphaSaturate || source_rgb_factor) {
    return NormalizeWebGLConstantBlendFactorFamily(factor);
  }
  return WebGLBlendFactor::One;
}

inline WebGLBlendState NormalizeWebGLBlendStateForContext(WebGLBlendState state) {
  state.src_rgb = SupportedWebGLBlendFactor(state.src_rgb, true);
  state.dst_rgb = SupportedWebGLBlendFactor(state.dst_rgb, false);
  state.src_alpha = SupportedWebGLBlendFactor(state.src_alpha, false);
  state.dst_alpha = SupportedWebGLBlendFactor(state.dst_alpha, false);
  state.rgb_op = SupportedWebGLBlendEquation(state.rgb_op);
  state.alpha_op = SupportedWebGLBlendEquation(state.alpha_op);
  return state;
}

inline void ApplyWebGLBlendState(const WebGLBlendState& raw_state) {
  const WebGLBlendState state = NormalizeWebGLBlendStateForContext(raw_state);
  if (BlendStateUsesConstantColor(state)) {
    const WebGLBlendColor color = BlendFactorColorToWebGL(state.blend_factor);
    glBlendColor(color.r, color.g, color.b, color.a);
  }

  if (BlendStateUsesSeparateFactors(state)) {
    glBlendFuncSeparate(
        WebGLEnum(state.src_rgb),
        WebGLEnum(state.dst_rgb),
        WebGLEnum(state.src_alpha),
        WebGLEnum(state.dst_alpha));
  } else {
    glBlendFunc(WebGLEnum(state.src_rgb), WebGLEnum(state.dst_rgb));
  }

  if (BlendStateUsesSeparateEquations(state)) {
    glBlendEquationSeparate(WebGLEnum(state.rgb_op), WebGLEnum(state.alpha_op));
  } else {
    glBlendEquation(WebGLEnum(state.rgb_op));
  }
}
#endif

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

inline WebGLBlendState BuildWebGLBlendState(const D3D9BlendRenderState& render_state) {
  return BuildWebGLBlendState(
      render_state.src_blend,
      render_state.dst_blend,
      render_state.blend_op,
      render_state.blend_factor,
      render_state.separate_alpha_blend_enable,
      render_state.src_blend_alpha,
      render_state.dst_blend_alpha,
      render_state.blend_op_alpha);
}

#ifdef __EMSCRIPTEN__
inline void ApplyWebGLBlendState(const D3D9BlendRenderState& render_state) {
  if (!render_state.alpha_blend_enable) {
    glDisable(GL_BLEND);
    return;
  }
  ApplyWebGLBlendState(BuildWebGLBlendState(render_state));
  glEnable(GL_BLEND);
}

inline bool ApplyD3D9BlendRenderStateValue(D3D9BlendRenderState* render_state,
                                           D3DRENDERSTATETYPE state,
                                           DWORD value) {
  if (!SetD3D9BlendRenderStateValue(render_state, state, value)) return false;
  ApplyWebGLBlendState(*render_state);
  return true;
}

inline bool ApplyD3D9BlendRenderStateValue(D3D9BlendRenderState& render_state,
                                           D3DRENDERSTATETYPE state,
                                           DWORD value) {
  return ApplyD3D9BlendRenderStateValue(&render_state, state, value);
}

inline void ApplyD3D9SpriteBlendRenderState(D3D9BlendRenderState* render_state) {
  if (!render_state) return;
  *render_state = MakeD3D9SpriteBlendRenderState();
  ApplyWebGLBlendState(*render_state);
}

inline void ApplyD3D9SpriteBlendRenderState(D3D9BlendRenderState& render_state) {
  ApplyD3D9SpriteBlendRenderState(&render_state);
}
#endif

}  // namespace wyd::d3d9_compat
