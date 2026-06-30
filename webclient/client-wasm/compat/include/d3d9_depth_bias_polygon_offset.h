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

struct D3D9DepthBiasPolygonOffsetScope {
  D3D9DepthBiasPolygonOffset offset{};
  bool active = false;

  explicit D3D9DepthBiasPolygonOffsetScope(
      const D3D9DepthBiasPolygonOffset& in_offset)
      : offset(in_offset) {}

  template <typename EnablePolygonOffsetFn, typename PolygonOffsetFn>
  void Begin(EnablePolygonOffsetFn enable_polygon_offset,
             PolygonOffsetFn polygon_offset) {
    if (!offset.enabled) return;
    enable_polygon_offset(true);
    polygon_offset(offset.factor, offset.units);
    active = true;
  }

  template <typename EnablePolygonOffsetFn>
  void End(EnablePolygonOffsetFn enable_polygon_offset) {
    if (!active) return;
    enable_polygon_offset(false);
    active = false;
  }
};

inline D3D9DepthBiasPolygonOffsetScope D3D9DepthBiasPolygonOffsetScopeFromRenderState(
    const D3D9DepthBiasRenderState& state) {
  return D3D9DepthBiasPolygonOffsetScope(
      D3D9DepthBiasPolygonOffsetFromRenderState(state));
}
