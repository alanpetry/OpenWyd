#pragma once

#include "d3d9.h"

namespace wyd::d3dx_compat {

constexpr DWORD kD3DXSpriteDoNotSaveState = 0x00000001u;
constexpr DWORD kD3DXSpriteDoNotModifyRenderState = 0x00000002u;
constexpr DWORD kD3DXSpriteObjectSpace = 0x00000004u;
constexpr DWORD kD3DXSpriteBillboard = 0x00000008u;
constexpr DWORD kD3DXSpriteAlphaBlend = 0x00000010u;
constexpr DWORD kD3DXSpriteSortTexture = 0x00000020u;
constexpr DWORD kD3DXSpriteSortDepthFrontToBack = 0x00000040u;
constexpr DWORD kD3DXSpriteSortDepthBackToFront = 0x00000080u;
constexpr DWORD kD3DXSpriteDoNotAddRefTexture = 0x00000100u;

struct D3DXSpriteBeginPolicy {
  bool save_device_state = true;
  bool modify_render_state = true;
  bool enable_alpha_blend = false;
  bool object_space = false;
  bool billboard = false;
  bool sort_texture = false;
  bool sort_depth_front_to_back = false;
  bool sort_depth_back_to_front = false;
  bool add_ref_texture = true;
};

inline D3DXSpriteBeginPolicy ResolveD3DXSpriteBeginPolicy(DWORD flags) {
  D3DXSpriteBeginPolicy policy;
  policy.save_device_state = (flags & kD3DXSpriteDoNotSaveState) == 0u;
  policy.modify_render_state = (flags & kD3DXSpriteDoNotModifyRenderState) == 0u;
  policy.enable_alpha_blend =
      policy.modify_render_state && ((flags & kD3DXSpriteAlphaBlend) != 0u);
  policy.object_space = (flags & kD3DXSpriteObjectSpace) != 0u;
  policy.billboard = (flags & kD3DXSpriteBillboard) != 0u;
  policy.sort_texture = (flags & kD3DXSpriteSortTexture) != 0u;
  policy.sort_depth_front_to_back = (flags & kD3DXSpriteSortDepthFrontToBack) != 0u;
  policy.sort_depth_back_to_front = (flags & kD3DXSpriteSortDepthBackToFront) != 0u;
  policy.add_ref_texture = (flags & kD3DXSpriteDoNotAddRefTexture) == 0u;
  return policy;
}

class D3DXSpriteHRESULTAccumulator {
 public:
  void Add(HRESULT hr) {
    if (FAILED(hr) && SUCCEEDED(first_failure_)) first_failure_ = hr;
  }

  HRESULT Result() const { return first_failure_; }

 private:
  HRESULT first_failure_ = S_OK;
};

inline HRESULT ApplyD3DXSpriteBeginRenderState(
    IDirect3DDevice9* device,
    const D3DXSpriteBeginPolicy& policy) {
  if (!device) return D3DERR_INVALIDCALL;
  if (!policy.modify_render_state) return S_OK;

  D3DXSpriteHRESULTAccumulator result;
  result.Add(device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));
  result.Add(device->SetRenderState(D3DRS_ZWRITEENABLE, 0u));
  result.Add(device->SetRenderState(D3DRS_ALPHATESTENABLE, 0u));
  result.Add(device->SetRenderState(D3DRS_LIGHTING, 0u));

  if (policy.enable_alpha_blend) {
    result.Add(device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1u));
    result.Add(device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    result.Add(device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
  }

  result.Add(device->SetVertexShader(nullptr));
  result.Add(device->SetPixelShader(nullptr));
  return result.Result();
}

inline HRESULT ApplyD3DXSpriteDrawRenderState(
    IDirect3DDevice9* device,
    const D3DXSpriteBeginPolicy& policy,
    IDirect3DTexture9* texture) {
  if (!device || !texture) return D3DERR_INVALIDCALL;

  D3DXSpriteHRESULTAccumulator result;
  result.Add(device->SetTexture(0, texture));
  result.Add(device->SetTexture(1, nullptr));
  result.Add(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
  result.Add(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
  result.Add(device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
  result.Add(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
  result.Add(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
  result.Add(device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
  result.Add(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE));
  result.Add(device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE));
  result.Add(device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0u));
  result.Add(device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1u));
  result.Add(device->SetFVF(324u));  // D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1

  if (policy.modify_render_state && policy.enable_alpha_blend) {
    result.Add(device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1u));
    result.Add(device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    result.Add(device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
  }

  return result.Result();
}

}  // namespace wyd::d3dx_compat