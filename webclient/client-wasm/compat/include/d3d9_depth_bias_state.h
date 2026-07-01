#pragma once

#include "d3d9.h"

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

inline bool D3D9DepthBiasHasOffset(const D3D9DepthBiasRenderState& state) {
  return state.slope_scale_depth_bias != 0u || state.depth_bias != 0u;
}

#ifdef __EMSCRIPTEN__
inline void ApplyWebGLDepthBiasState(const D3D9DepthBiasRenderState& state) {
  if (D3D9DepthBiasHasOffset(state)) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(state.SlopeScaleDepthBiasFloat(), state.DepthBiasFloat());
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
