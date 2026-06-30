#pragma once
#include "d3d9.h"

struct D3D9DepthBiasPolygonOffset {
  bool enabled = false;
  float factor = 0.0f;
  float units = 0.0f;
};

inline D3D9DepthBiasPolygonOffset D3D9DepthBiasPolygonOffsetFromRenderState(
    const D3D9DepthBiasRenderState& state) {
  D3D9DepthBiasPolygonOffset out{};
  out.factor = state.SlopeScaleDepthBiasFloat();
  out.units = state.DepthBiasFloat();
  out.enabled = (out.factor != 0.0f) || (out.units != 0.0f);
  return out;
}
