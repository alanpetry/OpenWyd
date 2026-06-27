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

  void ResetFromRenderState(const D3D9BlendRenderState& next_render_state) {
    render_state = next_render_state;
  }

  bool SetRenderStateValue(D3DRENDERSTATETYPE state, DWORD value) {
    return SetD3D9BlendRenderStateValue(render_state, state, value);
  }

  D3D9BlendRenderState& RenderState() { return render_state; }
  const D3D9BlendRenderState& RenderState() const { return render_state; }

  bool AlphaBlendEnabled() const { return render_state.alpha_blend_enable; }
  DWORD SrcBlend() const { return render_state.src_blend; }
  DWORD DstBlend() const { return render_state.dst_blend; }
  DWORD BlendOp() const { return render_state.blend_op; }
  DWORD BlendFactor() const { return render_state.blend_factor; }
  bool SeparateAlphaBlendEnabled() const { return render_state.separate_alpha_blend_enable; }
  DWORD SrcBlendAlpha() const { return render_state.src_blend_alpha; }
  DWORD DstBlendAlpha() const { return render_state.dst_blend_alpha; }
  DWORD BlendOpAlpha() const { return render_state.blend_op_alpha; }
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
  if (!current_state->SetRenderStateValue(state, value)) return false;
  ApplyD3D9BlendRuntimeState(*current_state);
  return true;
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

inline D3D9BlendRuntimeSnapshot ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRenderState& current_state) {
  D3D9BlendRuntimeSnapshot snapshot;
  ApplyD3D9SpriteBlendRuntimeState(current_state, &snapshot);
  return snapshot;
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

inline D3D9BlendRuntimeSnapshot ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRuntimeState& current_state) {
  D3D9BlendRuntimeSnapshot snapshot;
  ApplyD3D9SpriteBlendRuntimeState(current_state, &snapshot);
  return snapshot;
}

class ScopedD3D9SpriteBlendRuntimeState {
 public:
  explicit ScopedD3D9SpriteBlendRuntimeState(D3D9BlendRenderState& current_state)
      : render_state_(&current_state),
        snapshot_(ApplyD3D9SpriteBlendRuntimeState(current_state)),
        active_(true) {}

  explicit ScopedD3D9SpriteBlendRuntimeState(D3D9BlendRuntimeState& current_state)
      : runtime_state_(&current_state),
        snapshot_(ApplyD3D9SpriteBlendRuntimeState(current_state)),
        active_(true) {}

  ScopedD3D9SpriteBlendRuntimeState(const ScopedD3D9SpriteBlendRuntimeState&) = delete;
  ScopedD3D9SpriteBlendRuntimeState& operator=(const ScopedD3D9SpriteBlendRuntimeState&) = delete;

  ~ScopedD3D9SpriteBlendRuntimeState() { Restore(); }

  void Restore() {
    if (!active_) return;
    if (runtime_state_) {
      RestoreD3D9BlendRuntimeState(*runtime_state_, snapshot_);
    } else if (render_state_) {
      RestoreD3D9BlendRuntimeState(*render_state_, snapshot_);
    }
    active_ = false;
  }

 private:
  D3D9BlendRenderState* render_state_ = nullptr;
  D3D9BlendRuntimeState* runtime_state_ = nullptr;
  D3D9BlendRuntimeSnapshot snapshot_{};
  bool active_ = false;
};
#endif

}  // namespace wyd::d3d9_compat