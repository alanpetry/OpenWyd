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
#endif
