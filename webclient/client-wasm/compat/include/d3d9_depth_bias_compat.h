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

struct D3D9DepthBiasPolygonOffset {
  bool enabled = false;
  float factor = 0.0f;
  float units = 0.0f;
};

struct D3D9DepthBiasRenderStateUpdate {
  bool handled = false;
  D3D9DepthBiasPolygonOffset offset{};
};

inline float D3D9DepthBiasFloatFromDWORD(DWORD value) {
  float out = 0.0f;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

inline D3D9DepthBiasPolygonOffset BuildD3D9DepthBiasPolygonOffset(
    const D3D9DepthBiasRenderState& depth_bias_state) {
  D3D9DepthBiasPolygonOffset offset{};
  offset.enabled = depth_bias_state.enabled();
  if (!offset.enabled) return offset;

  // D3D9 stores both depth-bias render states as DWORD-preserved float bits.
  // WebGL's polygonOffset takes the same conceptual pair as factor/units.
  offset.factor = D3D9DepthBiasFloatFromDWORD(depth_bias_state.slope_scale_depth_bias);
  offset.units = D3D9DepthBiasFloatFromDWORD(depth_bias_state.depth_bias);
  return offset;
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

inline D3D9DepthBiasRenderStateUpdate ApplyD3D9DepthBiasRenderStateValue(
    D3D9DepthBiasRenderState* depth_bias_state,
    D3DRENDERSTATETYPE render_state,
    DWORD value) {
  D3D9DepthBiasRenderStateUpdate update{};
  update.handled = SetD3D9DepthBiasRenderStateValue(depth_bias_state, render_state, value);
  if (update.handled && depth_bias_state) {
    update.offset = BuildD3D9DepthBiasPolygonOffset(*depth_bias_state);
  }
  return update;
}
