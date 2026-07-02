#pragma once
#include "d3d9.h"

#include <cstring>

inline unsigned int D3D9FVFPositionMask(DWORD fvf) {
  return fvf & 0x400Eu;
}

inline unsigned int D3D9FVFPositionBlendCount(DWORD fvf) {
  switch (D3D9FVFPositionMask(fvf)) {
    case D3DFVF_XYZB1:
      return 1u;
    case D3DFVF_XYZB2:
      return 2u;
    case D3DFVF_XYZB3:
      return 3u;
    case D3DFVF_XYZB4:
      return 4u;
    case D3DFVF_XYZB5:
      return 5u;
    default:
      return 0u;
  }
}

inline bool D3D9FVFPositionHasRHW(DWORD fvf) {
  return D3D9FVFPositionMask(fvf) == D3DFVF_XYZRHW;
}

inline unsigned int D3D9FVFPositionBytes(DWORD fvf) {
  if (D3D9FVFPositionHasRHW(fvf)) return 16u;
  switch (D3D9FVFPositionMask(fvf)) {
    case D3DFVF_XYZ:
    case D3DFVF_XYZB1:
    case D3DFVF_XYZB2:
    case D3DFVF_XYZB3:
    case D3DFVF_XYZB4:
    case D3DFVF_XYZB5:
      break;
    default:
      return 0u;
  }

  unsigned int bytes = 12u;
  unsigned int blend_count = D3D9FVFPositionBlendCount(fvf);
  if (blend_count == 0u) return bytes;

  if ((fvf & (D3DFVF_LASTBETA_UBYTE4 | D3DFVF_LASTBETA_D3DCOLOR)) != 0u) {
    if (blend_count > 0u) blend_count -= 1u;
    bytes += 4u;
  }
  bytes += blend_count * 4u;
  return bytes;
}

inline unsigned int D3D9FVFNormalBytes(DWORD fvf) {
  return (fvf & D3DFVF_NORMAL) != 0u ? 12u : 0u;
}

inline bool D3D9FVFHasDiffuse(DWORD fvf) {
  return (fvf & D3DFVF_DIFFUSE) != 0u;
}

inline unsigned int D3D9FVFDiffuseBytes(DWORD fvf) {
  return D3D9FVFHasDiffuse(fvf) ? 4u : 0u;
}

inline bool D3D9FVFHasSpecular(DWORD fvf) {
  return (fvf & D3DFVF_SPECULAR) != 0u;
}

inline unsigned int D3D9FVFSpecularBytes(DWORD fvf) {
  return D3D9FVFHasSpecular(fvf) ? 4u : 0u;
}

inline unsigned int D3D9FVFBytesBeforeDiffuse(DWORD fvf) {
  return D3D9FVFPositionBytes(fvf) + D3D9FVFNormalBytes(fvf);
}

inline unsigned int D3D9FVFDiffuseOffset(DWORD fvf) {
  return D3D9FVFBytesBeforeDiffuse(fvf);
}

inline unsigned int D3D9FVFSpecularOffset(DWORD fvf) {
  return D3D9FVFBytesBeforeDiffuse(fvf) + D3D9FVFDiffuseBytes(fvf);
}

inline unsigned int D3D9FVFColorBytesBeforeTexcoords(DWORD fvf) {
  return D3D9FVFDiffuseBytes(fvf) + D3D9FVFSpecularBytes(fvf);
}

inline unsigned int D3D9FVFTexcoordOffset(DWORD fvf) {
  return D3D9FVFBytesBeforeDiffuse(fvf) + D3D9FVFColorBytesBeforeTexcoords(fvf);
}

inline unsigned int D3D9FVFTexcoordCount(DWORD fvf) {
  return (fvf >> 8) & 0xFu;
}

inline unsigned int D3D9FVFTexcoordBytes(DWORD fvf) {
  return D3D9FVFTexcoordCount(fvf) * 8u;
}

inline unsigned int D3D9FVFVertexBytes(DWORD fvf) {
  return D3D9FVFTexcoordOffset(fvf) + D3D9FVFTexcoordBytes(fvf);
}

struct D3D9FVFDecodeLayout {
  bool valid_position = false;
  bool has_rhw = false;
  bool has_normal = false;
  bool has_diffuse = false;
  bool has_specular = false;
  unsigned int position_bytes = 0u;
  unsigned int normal_offset = 0u;
  unsigned int diffuse_offset = 0u;
  unsigned int specular_offset = 0u;
  unsigned int texcoord_offset = 0u;
  unsigned int texcoord_count = 0u;
  unsigned int vertex_bytes = 0u;
};

inline D3D9FVFDecodeLayout D3D9FVFBuildDecodeLayout(DWORD fvf) {
  D3D9FVFDecodeLayout layout{};
  layout.position_bytes = D3D9FVFPositionBytes(fvf);
  layout.valid_position = layout.position_bytes != 0u;
  layout.has_rhw = D3D9FVFPositionHasRHW(fvf);
  layout.has_normal = D3D9FVFNormalBytes(fvf) != 0u;
  layout.has_diffuse = D3D9FVFHasDiffuse(fvf);
  layout.has_specular = D3D9FVFHasSpecular(fvf);
  layout.normal_offset = layout.position_bytes;
  layout.diffuse_offset = D3D9FVFDiffuseOffset(fvf);
  layout.specular_offset = D3D9FVFSpecularOffset(fvf);
  layout.texcoord_offset = D3D9FVFTexcoordOffset(fvf);
  layout.texcoord_count = D3D9FVFTexcoordCount(fvf);
  layout.vertex_bytes = D3D9FVFVertexBytes(fvf);
  return layout;
}

struct D3D9FVFDecodedColorTexcoords {
  bool valid = false;
  DWORD diffuse = 0xFFFFFFFFu;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  unsigned int texcoords_read = 0u;
};

inline bool D3D9FVFCanReadField(unsigned int offset, unsigned int bytes, unsigned int stride) {
  return bytes == 0u || (offset <= stride && bytes <= stride - offset);
}

template <typename T>
inline T D3D9FVFReadUnalignedField(const unsigned char* ptr) {
  T out{};
  if (ptr) std::memcpy(&out, ptr, sizeof(T));
  return out;
}

inline bool D3D9FVFLayoutFitsStride(const D3D9FVFDecodeLayout& layout, unsigned int stride) {
  if (!layout.valid_position) return false;
  return layout.vertex_bytes <= stride;
}

inline D3D9FVFDecodedColorTexcoords D3D9FVFDecodeColorTexcoords(
    const unsigned char* src,
    unsigned int stride,
    DWORD fvf) {
  D3D9FVFDecodedColorTexcoords out{};
  if (!src) return out;

  const D3D9FVFDecodeLayout layout = D3D9FVFBuildDecodeLayout(fvf);
  if (!D3D9FVFLayoutFitsStride(layout, stride)) return out;

  if (layout.has_diffuse) {
    if (!D3D9FVFCanReadField(layout.diffuse_offset, 4u, stride)) return out;
    out.diffuse = D3D9FVFReadUnalignedField<DWORD>(src + layout.diffuse_offset);
  }

  if (layout.texcoord_count > 0u) {
    if (!D3D9FVFCanReadField(layout.texcoord_offset, 8u, stride)) return out;
    out.u0 = D3D9FVFReadUnalignedField<float>(src + layout.texcoord_offset + 0u);
    out.v0 = D3D9FVFReadUnalignedField<float>(src + layout.texcoord_offset + 4u);
    out.texcoords_read = 1u;
  }

  if (layout.texcoord_count > 1u) {
    if (!D3D9FVFCanReadField(layout.texcoord_offset + 8u, 8u, stride)) return out;
    out.u1 = D3D9FVFReadUnalignedField<float>(src + layout.texcoord_offset + 8u);
    out.v1 = D3D9FVFReadUnalignedField<float>(src + layout.texcoord_offset + 12u);
    out.texcoords_read = 2u;
  } else {
    out.u1 = out.u0;
    out.v1 = out.v0;
  }

  out.valid = true;
  return out;
}