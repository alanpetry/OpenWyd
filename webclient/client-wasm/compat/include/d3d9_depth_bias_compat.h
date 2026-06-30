#pragma once

#include "d3d9.h"

#include <cstring>

// Numeric D3D9 render states used by shadow/decal-style depth-bias passes.
// Keeping these symbols in the compact WASM D3D9 surface lets production
// renderer code map the original Direct3D contract to WebGL polygonOffset
// without hard-coded state ids at call sites.
#ifndef D3DRS_SLOPESCALEDEPTHBIAS
#define D3DRS_SLOPESCALEDEPTHBIAS static_cast<D3DRENDERSTATETYPE>(175)
#endif

#ifndef D3DRS_DEPTHBIAS
#define D3DRS_DEPTHBIAS static_cast<D3DRENDERSTATETYPE>(195)
#endif

struct D3D9DepthBiasRenderState {
  DWORD slope_scale_depth_bias = 0;
  DWORD depth_bias = 0;

  bool enabled() const {
    return slope_scale_depth_bias != 0u || depth_bias != 0u;
  }
};

inline float D3D9DepthBiasFloatFromDWORD(DWORD value) {
  float out = 0.0f;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

inline bool SetD3D9DepthBiasRenderStateValue(
    D3D9DepthBiasRenderState* depth_bias_state,
    D3DRENDERSTATETYPE render_state,
    DWORD value) {
  if (!depth_bias_state) return false;
  switch (render_state) {
    case D3DRS_SLOPESCALEDEPTHBIAS:
      depth_bias_state->slope_scale_depth_bias = value;
      return true;
    case D3DRS_DEPTHBIAS:
      depth_bias_state->depth_bias = value;
      return true;
    default:
      return false;
  }
}
