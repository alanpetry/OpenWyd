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

}  // namespace wyd::d3dx_compat
