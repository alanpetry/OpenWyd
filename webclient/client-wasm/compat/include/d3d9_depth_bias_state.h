#pragma once

#include "d3d9.h"

#include <cmath>

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

inline float D3D9DepthBiasFiniteOrZero(float value) {
  return std::isfinite(value) ? value : 0.0f;
}

inline float D3D9DepthBiasSlopeScale(const D3D9DepthBiasRenderState& state) {
  return D3D9DepthBiasFiniteOrZero(state.SlopeScaleDepthBiasFloat());
}

inline float D3D9DepthBiasConstant(const D3D9DepthBiasRenderState& state) {
  return D3D9DepthBiasFiniteOrZero(state.DepthBiasFloat());
}

inline bool D3D9DepthBiasHasOffset(const D3D9DepthBiasRenderState& state) {
  return D3D9DepthBiasSlopeScale(state) != 0.0f || D3D9DepthBiasConstant(state) != 0.0f;
}

#ifdef __EMSCRIPTEN__
inline void ApplyWebGLDepthBiasState(const D3D9DepthBiasRenderState& state) {
  const float slope_scale = D3D9DepthBiasSlopeScale(state);
  const float constant = D3D9DepthBiasConstant(state);
  if (slope_scale != 0.0f || constant != 0.0f) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(slope_scale, constant);
  } else {
    glPolygonOffset(0.0f, 0.0f);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
}

inline bool UpdateAndApplyWebGLDepthBiasRenderState(
    D3D9DepthBiasRenderState* state,
    D3DRENDERSTATETYPE type,
    DWORD value) {
  if (!SetD3D9DepthBiasRenderStateValue(state, type, value)) return false;
  ApplyWebGLDepthBiasState(*state);
  return true;
}
#else
inline void ApplyWebGLDepthBiasState(const D3D9DepthBiasRenderState&) {}

inline bool UpdateAndApplyWebGLDepthBiasRenderState(
    D3D9DepthBiasRenderState* state,
    D3DRENDERSTATETYPE type,
    DWORD value) {
  return SetD3D9DepthBiasRenderStateValue(state, type, value);
}
#endif
