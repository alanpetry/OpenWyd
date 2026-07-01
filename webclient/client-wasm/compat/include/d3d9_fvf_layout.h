#pragma once
#include "d3d9.h"

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
