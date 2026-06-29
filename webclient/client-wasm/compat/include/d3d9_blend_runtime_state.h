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

struct D3D9LegacyBlendFields {
  bool alpha_blend_enable = false;
  DWORD src_blend = D3DBLEND_ONE;
  DWORD dst_blend = D3DBLEND_ZERO;
};

inline D3D9LegacyBlendFields CaptureD3D9LegacyBlendFields(
    const D3D9BlendRenderState& render_state) {
  D3D9LegacyBlendFields fields;
  fields.alpha_blend_enable = render_state.alpha_blend_enable;
  fields.src_blend = render_state.src_blend;
  fields.dst_blend = render_state.dst_blend;
  return fields;
}

inline D3D9LegacyBlendFields CaptureD3D9LegacyBlendFields(
    const D3D9BlendRuntimeState& runtime_state) {
  return CaptureD3D9LegacyBlendFields(runtime_state.RenderState());
}

inline void SyncD3D9LegacyBlendFields(
    const D3D9BlendRenderState& render_state,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  const D3D9LegacyBlendFields fields = CaptureD3D9LegacyBlendFields(render_state);
  if (alpha_blend_enable) *alpha_blend_enable = fields.alpha_blend_enable;
  if (src_blend) *src_blend = fields.src_blend;
  if (dst_blend) *dst_blend = fields.dst_blend;
}

inline void SyncD3D9LegacyBlendFields(
    const D3D9BlendRuntimeState& runtime_state,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  SyncD3D9LegacyBlendFields(
      runtime_state.RenderState(),
      alpha_blend_enable,
      src_blend,
      dst_blend);
}

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
  const auto apply_alpha_blend_enable = [&]() {
    apply(
        D3DRS_ALPHABLENDENABLE,
        render_state.alpha_blend_enable ? 1u : 0u);
  };

  if (!render_state.alpha_blend_enable) apply_alpha_blend_enable();
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
  if (render_state.alpha_blend_enable) apply_alpha_blend_enable();
  if (first_error == S_OK) ApplyWebGLBlendState(render_state);
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

inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRuntimeState& current_state,
    const D3D9BlendRuntimeSnapshot& snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  RestoreD3D9BlendRuntimeState(current_state, snapshot);
  SyncD3D9LegacyBlendFields(
      current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
}

inline void RestoreD3D9BlendRuntimeState(
    D3D9BlendRenderState& current_state,
    const D3D9BlendRuntimeSnapshot& snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  RestoreD3D9BlendRuntimeState(current_state, snapshot);
  SyncD3D9LegacyBlendFields(
      current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
}

inline HRESULT RestoreD3D9BlendRuntimeState(
    IDirect3DDevice9* device,
    const D3D9BlendRuntimeSnapshot& snapshot) {
  return ApplyD3D9BlendRenderStateToDevice(device, snapshot.render_state);
}

inline HRESULT RestoreD3D9BlendRuntimeState(
    IDirect3DDevice9* device,
    const D3D9BlendRuntimeSnapshot& snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  const HRESULT result = RestoreD3D9BlendRuntimeState(device, snapshot);
  if (result != S_OK) return result;
  SyncD3D9LegacyBlendFields(
      snapshot.render_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
  return result;
}

inline HRESULT RestoreD3D9BlendRuntimeState(
    IDirect3DDevice9* device,
    D3D9BlendRuntimeState& current_state,
    const D3D9BlendRuntimeSnapshot& snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  const HRESULT result = RestoreD3D9BlendRuntimeState(device, snapshot);
  if (result != S_OK) return result;
  current_state.ResetFromRenderState(snapshot.render_state);
  SyncD3D9LegacyBlendFields(
      current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
  return result;
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

inline bool ApplyD3D9BlendRuntimeStateValue(
    IDirect3DDevice9* device,
    D3D9BlendRuntimeState* current_state,
    D3DRENDERSTATETYPE state,
    DWORD value) {
  if (!current_state) return false;

  D3D9BlendRuntimeState next_state = *current_state;
  if (!next_state.SetRenderStateValue(state, value)) return false;

  if (!device) {
    current_state->ResetFromRenderState(next_state.RenderState());
    ApplyD3D9BlendRuntimeState(*current_state);
    return true;
  }

  const HRESULT result = ApplyD3D9BlendRenderStateToDevice(device, next_state.RenderState());
  if (result != S_OK) return false;
  current_state->ResetFromRenderState(next_state.RenderState());
  return true;
}

inline bool ApplyD3D9BlendRuntimeStateValue(
    IDirect3DDevice9* device,
    D3D9BlendRuntimeState* current_state,
    D3DRENDERSTATETYPE state,
    DWORD value,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  if (!ApplyD3D9BlendRuntimeStateValue(device, current_state, state, value)) {
    return false;
  }
  SyncD3D9LegacyBlendFields(
      *current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
  return true;
}

inline bool ApplyD3D9BlendRuntimeStateValue(
    IDirect3DDevice9* device,
    D3D9BlendRuntimeState& current_state,
    D3DRENDERSTATETYPE state,
    DWORD value) {
  return ApplyD3D9BlendRuntimeStateValue(device, &current_state, state, value);
}

inline bool ApplyD3D9BlendRuntimeStateValue(
    IDirect3DDevice9* device,
    D3D9BlendRuntimeState& current_state,
    D3DRENDERSTATETYPE state,
    DWORD value,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  return ApplyD3D9BlendRuntimeStateValue(
      device,
      &current_state,
      state,
      value,
      alpha_blend_enable,
      src_blend,
      dst_blend);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRenderState* current_state,
    D3D9BlendRuntimeSnapshot* snapshot) {
  if (!current_state) return;
  if (snapshot) *snapshot = CaptureD3D9BlendRuntimeState(*current_state);
  *current_state = MakeD3D9SpriteBlendRenderState();
  ApplyD3D9BlendRuntimeState(*current_state);
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

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRuntimeState& current_state,
    D3D9BlendRuntimeSnapshot* snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  ApplyD3D9SpriteBlendRuntimeState(current_state, snapshot);
  SyncD3D9LegacyBlendFields(
      current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
}

inline void ApplyD3D9SpriteBlendRuntimeState(
    D3D9BlendRenderState& current_state,
    D3D9BlendRuntimeSnapshot* snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  ApplyD3D9SpriteBlendRuntimeState(current_state, snapshot);
  SyncD3D9LegacyBlendFields(
      current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
}

inline HRESULT ApplyD3D9SpriteBlendRuntimeState(
    IDirect3DDevice9* device,
    D3D9BlendRuntimeState& current_state,
    D3D9BlendRuntimeSnapshot* snapshot,
    bool* alpha_blend_enable,
    DWORD* src_blend,
    DWORD* dst_blend) {
  if (snapshot) *snapshot = CaptureD3D9BlendRuntimeState(current_state);
  const D3D9BlendRenderState sprite_state = MakeD3D9SpriteBlendRenderState();
  const HRESULT result = ApplyD3D9BlendRenderStateToDevice(device, sprite_state);
  if (result != S_OK) return result;
  current_state.ResetFromRenderState(sprite_state);
  SyncD3D9LegacyBlendFields(
      current_state,
      alpha_blend_enable,
      src_blend,
      dst_blend);
  return result;
}

inline HRESULT ApplyD3D9SpriteBlendRuntimeState(
    IDirect3DDevice9* device,
    bool alpha_blend_enable,
    DWORD src_blend,
    DWORD dst_blend,
    D3D9BlendRuntimeSnapshot* snapshot,
    bool* alpha_blend_enable_out,
    DWORD* src_blend_out,
    DWORD* dst_blend_out) {
  D3D9BlendRuntimeState current_state(
      alpha_blend_enable,
      src_blend,
      dst_blend);
  return ApplyD3D9SpriteBlendRuntimeState(
      device,
      current_state,
      snapshot,
      alpha_blend_enable_out,
      src_blend_out,
      dst_blend_out);
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

  ScopedD3D9SpriteBlendRuntimeState(
      D3D9BlendRuntimeState& current_state,
      bool* alpha_blend_enable,
      DWORD* src_blend,
      DWORD* dst_blend)
      : runtime_state_(&current_state),
        alpha_blend_enable_(alpha_blend_enable),
        src_blend_(src_blend),
        dst_blend_(dst_blend),
        active_(true) {
    ApplyD3D9SpriteBlendRuntimeState(
        current_state,
        &snapshot_,
        alpha_blend_enable_,
        src_blend_,
        dst_blend_);
  }

  ScopedD3D9SpriteBlendRuntimeState(
      D3D9BlendRenderState& current_state,
      bool* alpha_blend_enable,
      DWORD* src_blend,
      DWORD* dst_blend)
      : render_state_(&current_state),
        alpha_blend_enable_(alpha_blend_enable),
        src_blend_(src_blend),
        dst_blend_(dst_blend),
        active_(true) {
    ApplyD3D9SpriteBlendRuntimeState(
        current_state,
        &snapshot_,
        alpha_blend_enable_,
        src_blend_,
        dst_blend_);
  }

  ScopedD3D9SpriteBlendRuntimeState(const ScopedD3D9SpriteBlendRuntimeState&) = delete;
  ScopedD3D9SpriteBlendRuntimeState& operator=(const ScopedD3D9SpriteBlendRuntimeState&) = delete;

  ~ScopedD3D9SpriteBlendRuntimeState() { Restore(); }

  void Restore() {
    if (!active_) return;
    if (runtime_state_) {
      RestoreD3D9BlendRuntimeState(
          *runtime_state_,
          snapshot_,
          alpha_blend_enable_,
          src_blend_,
          dst_blend_);
    } else if (render_state_) {
      RestoreD3D9BlendRuntimeState(
          *render_state_,
          snapshot_,
          alpha_blend_enable_,
          src_blend_,
          dst_blend_);
    }
    active_ = false;
  }

 private:
  D3D9BlendRenderState* render_state_ = nullptr;
  D3D9BlendRuntimeState* runtime_state_ = nullptr;
  bool* alpha_blend_enable_ = nullptr;
  DWORD* src_blend_ = nullptr;
  DWORD* dst_blend_ = nullptr;
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
      D3D9BlendRuntimeState& runtime_state,
      bool* alpha_blend_enable,
      DWORD* src_blend,
      DWORD* dst_blend)
      : device_(device),
        runtime_state_(&runtime_state),
        alpha_blend_enable_(alpha_blend_enable),
        src_blend_(src_blend),
        dst_blend_(dst_blend),
        active_(device != nullptr) {
    apply_result_ = ApplyD3D9SpriteBlendRuntimeState(
        device_,
        runtime_state,
        &snapshot_,
        alpha_blend_enable_,
        src_blend_,
        dst_blend_);
  }

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

  ScopedD3D9SpriteDeviceBlendRuntimeState(
      IDirect3DDevice9* device,
      bool alpha_blend_enable,
      DWORD src_blend,
      DWORD dst_blend,
      bool* alpha_blend_enable_out,
      DWORD* src_blend_out,
      DWORD* dst_blend_out)
      : device_(device),
        alpha_blend_enable_(alpha_blend_enable_out),
        src_blend_(src_blend_out),
        dst_blend_(dst_blend_out),
        active_(device != nullptr) {
    apply_result_ = ApplyD3D9SpriteBlendRuntimeState(
        device_,
        alpha_blend_enable,
        src_blend,
        dst_blend,
        &snapshot_,
        alpha_blend_enable_,
        src_blend_,
        dst_blend_);
  }

  ScopedD3D9SpriteDeviceBlendRuntimeState(const ScopedD3D9SpriteDeviceBlendRuntimeState&) = delete;
  ScopedD3D9SpriteDeviceBlendRuntimeState& operator=(const ScopedD3D9SpriteDeviceBlendRuntimeState&) = delete;

  ~ScopedD3D9SpriteDeviceBlendRuntimeState() { Restore(); }

  HRESULT ApplyResult() const { return apply_result_; }
  HRESULT RestoreResult() const { return restore_result_; }

  HRESULT Restore() {
    if (!active_) return restore_result_;
    if (runtime_state_) {
      restore_result_ = RestoreD3D9BlendRuntimeState(
          device_,
          *runtime_state_,
          snapshot_,
          alpha_blend_enable_,
          src_blend_,
          dst_blend_);
    } else {
      restore_result_ = RestoreD3D9BlendRuntimeState(
          device_,
          snapshot_,
          alpha_blend_enable_,
          src_blend_,
          dst_blend_);
    }
    active_ = false;
    return restore_result_;
  }

 private:
  IDirect3DDevice9* device_ = nullptr;
  D3D9BlendRuntimeState* runtime_state_ = nullptr;
  bool* alpha_blend_enable_ = nullptr;
  DWORD* src_blend_ = nullptr;
  DWORD* dst_blend_ = nullptr;
  D3D9BlendRuntimeSnapshot snapshot_{};
  HRESULT apply_result_ = S_OK;
  HRESULT restore_result_ = S_OK;
  bool active_ = false;
};
#endif

}  // namespace wyd::d3d9_compat
