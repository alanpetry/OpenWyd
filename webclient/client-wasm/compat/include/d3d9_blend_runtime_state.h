#pragma once

#include "d3d9_blend_state.h"

namespace wyd::d3d9_compat {

struct D3D9BlendRuntimeState {
  D3D9BlendRenderState render_state;

  bool AlphaBlendEnabled() const { return render_state.alpha_blend_enable; }
  DWORD SrcBlend() const { return render_state.src_blend; }
  DWORD DstBlend() const { return render_state.dst_blend; }
};

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
  return CaptureD3D9BlendRuntimeState(runtime_state.render_state);
}

#ifdef __EMSCRIPTEN__
inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRenderState* current_state,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  if (!current_state) return;
  *current_state = snapshot.render_state;
  ApplyWebGLBlendState(*current_state);
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
  RestoreD3D9BlendRuntimeState(current_state->render_state, snapshot);
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
  return ApplyD3D9BlendRenderStateValue(current_state->render_state, state, value);
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
  ApplyD3D9SpriteBlendRuntimeState(current_state->render_state, snapshot);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRuntimeState& current_state,
    D3D9BlendRuntimeSnapshot* snapshot) {
  ApplyD3D9SpriteBlendRuntimeState(&current_state, snapshot);
}
#endif

}  // namespace wyd::d3d9_compat
