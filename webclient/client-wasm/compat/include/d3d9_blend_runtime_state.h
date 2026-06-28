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

inline D3D9BlendRuntimeSnapshot CaptureD3D9BlendRuntimeState(
    bool alpha_blend_enable,
    DWORD src_blend,
    DWORD dst_blend) {
  return CaptureD3D9BlendRuntimeState(
      MakeD3D9BlendRenderState(alpha_blend_enable, src_blend, dst_blend));
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

inline HRESULT ApplyD3D9BlendRenderStateToDevice(
    IDirect3DDevice9* device,
    const D3D9BlendRenderState& render_state) {
  if (!device) return D3DERR_INVALIDCALL;
  HRESULT first_error = S_OK;
  const auto apply = [&](D3DRENDERSTATETYPE state, DWORD value) {
    const HRESULT result = device->SetRenderState(state, value);
    if (first_error == S_OK && result != S_OK) first_error = result;
  };

  apply(D3DRS_SRCBLEND, render_state.src_blend);
  apply(D3DRS_DESTBLEND, render_state.dst_blend);
  apply(D3DRS_BLENDOP, render_state.blend_op);
  apply(D3DRS_BLENDFACTOR, render_state.blend_factor);
  apply(
      D3DRS_SEPARATEALPHABLENDENABLE,
      render_state.separate_alpha_blend_enable ? 1u : 0u);
  apply(D3DRS_SRCBLENDALPHA, render_state.src_blend_alpha);
  apply(D3DRS_DESTBLENDALPHA, render_state.dst_blend_alpha);
  apply(D3DRS_BLENDOPALPHA, render_state.blend_op_alpha);
  apply(
      D3DRS_ALPHABLENDENABLE,
      render_state.alpha_blend_enable ? 1u : 0u);
  ApplyWebGLBlendState(render_state);
  return first_error;
}

inline HRESULT ApplyD3D9SpriteBlendRuntimeState(IDirect3DDevice9* device) {
  return ApplyD3D9BlendRenderStateToDevice(device, MakeD3D9SpriteBlendRenderState());
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

inline HRESULT RestoreD3D9BlendRuntimeState(
    IDirect3DDevice9* device,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  return ApplyD3D9BlendRenderStateToDevice(device, snapshot.render_state);
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

class ScopedD3D9SpriteDeviceBlendRuntimeState {
 public:
  ScopedD3D9SpriteDeviceBlendRuntimeState(
      IDirect3DDevice9* device,
      const D3D9BlendRuntimeSnapshot& snapshot)
      : device_(device), snapshot_(snapshot), active_(device != nullptr) {
    apply_result_ = ApplyD3D9SpriteBlendRuntimeState(device_);
  }

  ScopedD3D9SpriteDeviceBlendRuntimeState(
      IDirect3DDevice9* device,
      const D3D9BlendRenderState& render_state)
      : ScopedD3D9SpriteDeviceBlendRuntimeState(
            device,
            CaptureD3D9BlendRuntimeState(render_state)) {}

  ScopedD3D9SpriteDeviceBlendRuntimeState(
      IDirect3DDevice9* device,
      const D3D9BlendRuntimeState& runtime_state)
      : ScopedD3D9SpriteDeviceBlendRuntimeState(
            device,
            CaptureD3D9BlendRuntimeState(runtime_state)) {}

  ScopedD3D9SpriteDeviceBlendRuntimeState(
      IDirect3DDevice9* device,
      bool alpha_blend_enable,
      DWORD src_blend,
      DWORD dst_blend)
      : ScopedD3D9SpriteDeviceBlendRuntimeState(
            device,
            MakeD3D9BlendRenderState(
                alpha_blend_enable,
                src_blend,
                dst_blend)) {}

  ScopedD3D9SpriteDeviceBlendRuntimeState(const ScopedD3D9SpriteDeviceBlendRuntimeState&) = delete;
  ScopedD3D9SpriteDeviceBlendRuntimeState& operator=(const ScopedD3D9SpriteDeviceBlendRuntimeState&) = delete;

  ~ScopedD3D9SpriteDeviceBlendRuntimeState() { Restore(); }

  HRESULT ApplyResult() const { return apply_result_; }
  HRESULT RestoreResult() const { return restore_result_; }

  HRESULT Restore() {
    if (!active_) return restore_result_;
    restore_result_ = RestoreD3D9BlendRuntimeState(device_, snapshot_);
    active_ = false;
    return restore_result_;
  }

 private:
  IDirect3DDevice9* device_ = nullptr;
  D3D9BlendRuntimeSnapshot snapshot_{};
  HRESULT apply_result_ = S_OK;
  HRESULT restore_result_ = S_OK;
  bool active_ = false;
};
#endif

}  // namespace wyd::d3d9_compat
