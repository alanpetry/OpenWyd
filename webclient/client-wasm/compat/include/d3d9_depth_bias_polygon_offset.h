#pragma once
#include "d3d9.h"

#include <cmath>

struct D3D9DepthBiasPolygonOffset {
  bool enabled = false;
  float factor = 0.0f;
  float units = 0.0f;
};

inline float D3D9DepthBiasFinitePolygonOffsetValue(float value) {
  return std::isfinite(value) ? value : 0.0f;
}

inline float D3D9DepthBiasPolygonOffsetUnitsFromDepthBias(float depth_bias) {
  constexpr float kDefaultWebGLDepthUnitScale = 16777216.0f;
  return D3D9DepthBiasFinitePolygonOffsetValue(depth_bias) * kDefaultWebGLDepthUnitScale;
}

inline D3D9DepthBiasPolygonOffset D3D9DepthBiasPolygonOffsetFromRenderState(
    const D3D9DepthBiasRenderState& state) {
  D3D9DepthBiasPolygonOffset out{};
  out.factor = D3D9DepthBiasFinitePolygonOffsetValue(state.SlopeScaleDepthBiasFloat());
  out.units = D3D9DepthBiasPolygonOffsetUnitsFromDepthBias(state.DepthBiasFloat());
  out.enabled = (out.factor != 0.0f) || (out.units != 0.0f);
  return out;
}

inline bool D3D9DepthBiasRenderStateAppliesPolygonOffset(
    const D3D9DepthBiasRenderState& state) {
  return D3D9DepthBiasPolygonOffsetFromRenderState(state).enabled;
}

inline bool SetD3D9DepthBiasRenderStateValueAndCheckActive(
    D3D9DepthBiasRenderState* state,
    D3DRENDERSTATETYPE type,
    DWORD value) {
  if (!SetD3D9DepthBiasRenderStateValue(state, type, value) || !state) return false;
  return D3D9DepthBiasRenderStateAppliesPolygonOffset(*state);
}

struct D3D9DepthBiasPolygonOffsetScope {
  D3D9DepthBiasPolygonOffset offset{};
  bool active = false;

  explicit D3D9DepthBiasPolygonOffsetScope(
      const D3D9DepthBiasPolygonOffset& in_offset)
      : offset(in_offset) {}

  bool IsEnabled() const { return offset.enabled; }

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

  template <typename EnablePolygonOffsetFn, typename PolygonOffsetFn>
  void End(EnablePolygonOffsetFn enable_polygon_offset,
           PolygonOffsetFn polygon_offset) {
    if (!active) return;
    polygon_offset(0.0f, 0.0f);
    End(enable_polygon_offset);
  }

  template <typename EnablePolygonOffsetFn, typename PolygonOffsetFn, typename DrawFn>
  void AroundDraw(EnablePolygonOffsetFn enable_polygon_offset,
                  PolygonOffsetFn polygon_offset,
                  DrawFn draw) {
    Begin(enable_polygon_offset, polygon_offset);
    draw();
    End(enable_polygon_offset, polygon_offset);
  }
};

inline D3D9DepthBiasPolygonOffsetScope D3D9DepthBiasPolygonOffsetScopeFromRenderState(
    const D3D9DepthBiasRenderState& state) {
  return D3D9DepthBiasPolygonOffsetScope(
      D3D9DepthBiasPolygonOffsetFromRenderState(state));
}
