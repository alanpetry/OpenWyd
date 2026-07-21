#pragma once

#include "d3d9_blend_state.h"

namespace wyd::d3d9_compat {

struct D3D9BlendRuntimeState {
  D3D9BlendRenderState render_state;

  D3D9BlendRuntimeState() = default;

  D3D9BlendRuntimeState(bool alpha_blend_enable,
                        DWORD src_blend,
                        DWORD dst_blend) {
    ResetFromLegacyFields(alpha_blend_enable, src_blend, dst_blend);
  }

  void ResetFromLegacyFields(bool alpha_blend_enable,
                             DWORD src_blend,
                             DWORD dst_blend) {
    render_state = MakeD3D9BlendRenderState(alpha_blend_enable, src_blend, dst_blend);
  }

  D3D9BlendRenderState& RenderState() { return render_state; }
  const D3D9BlendRenderState& RenderState() const { return render_state; }

  bool AlphaBlendEnabled() const { return render_state.alpha_blend_enable; }
  DWORD SrcBlend() const { return render_state.src_blend; }
  DWORD DstBlend() const { return render_state.dst_blend; }
};

inline D3D9BlendRuntimeState MakeD3D9BlendRuntimeState(
    bool alpha_blend_enable,
    DWORD src_blend,
    DWORD dst_blend) {
  return D3D9BlendRuntimeState(alpha_blend_enable, src_blend, dst_blend);
}

struct D3D9BlendRuntimeSnapshot {
  D3D9BlendRenderState render_state;
};

inline D3D9BlendRuntimeSnapshot CaptureD3D9BlendRuntimeState(
    const D3D9BlendRenderState& render_state) {
  D3D9BlendRuntimeSnapshot snapshot;
  snapshot.render_state = render_state;
  return snapshot;
}

inline D3D9BlendRuntimeSnapshot CaptureD3D9BlendRuntimeState(
    const D3D9BlendRuntimeState& runtime_state) {
  return CaptureD3D9BlendRuntimeState(runtime_state.RenderState());
}

#ifdef __EMSCRIPTEN__
inline void ApplyD3D9BlendRuntimeState(const D3D9BlendRenderState* current_state) {
  if (!current_state) return;
  ApplyWebGLBlendState(*current_state);
}

inline void ApplyD3D9BlendRuntimeState(const D3D9BlendRenderState& current_state) {
  ApplyD3D9BlendRuntimeState(&current_state);
}

inline void ApplyD3D9BlendRuntimeState(const D3D9BlendRuntimeState* current_state) {
  if (!current_state) return;
  ApplyD3D9BlendRuntimeState(current_state->RenderState());
}

inline void ApplyD3D9BlendRuntimeState(const D3D9BlendRuntimeState& current_state) {
  ApplyD3D9BlendRuntimeState(&current_state);
}

inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRenderState* current_state,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  if (!current_state) return;
  *current_state = snapshot.render_state;
  ApplyD3D9BlendRuntimeState(*current_state);
}

inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRenderState& current_state,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  RestoreD3D9BlendRuntimeState(&current_state, snapshot);
}

inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRuntimeState* current_state,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  if (!current_state) return;
  RestoreD3D9BlendRuntimeState(current_state->RenderState(), snapshot);
}

inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRuntimeState& current_state,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  RestoreD3D9BlendRuntimeState(&current_state, snapshot);
}

inline bool ApplyD3D9BlendRuntimeStateValue(
    D3D9BlendRuntimeState* current_state,
    D3DRENDERSTATETYPE state,
    DWORD value) {
  if (!current_state) return false;
  return ApplyD3D9BlendRenderStateValue(current_state->RenderState(), state, value);
}

inline bool ApplyD3D9BlendRuntimeStateValue(
    D3D9BlendRuntimeState& current_state,
    D3DRENDERSTATETYPE state,
    DWORD value) {
  return ApplyD3D9BlendRuntimeStateValue(&current_state, state, value);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRenderState* current_state,
    D3D9BlendRuntimeSnapshot* snapshot) {
  if (!current_state) return;
  if (snapshot) *snapshot = CaptureD3D9BlendRuntimeState(*current_state);
  ApplyD3D9SpriteBlendRenderState(current_state);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRenderState& current_state,
    D3D9BlendRuntimeSnapshot* snapshot) {
  ApplyD3D9SpriteBlendRuntimeState(&current_state, snapshot);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRuntimeState* current_state,
    D3D9BlendRuntimeSnapshot* snapshot) {
  if (!current_state) return;
  ApplyD3D9SpriteBlendRuntimeState(current_state->RenderState(), snapshot);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRuntimeState& current_state,
    D3D9BlendRuntimeSnapshot* snapshot) {
  ApplyD3D9SpriteBlendRuntimeState(&current_state, snapshot);
}
#endif

}  // namespace wyd::d3d9_compat