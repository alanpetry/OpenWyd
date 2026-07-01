#pragma once
#include "d3d9.h"

inline unsigned int D3D9FVFPositionBytes(DWORD fvf) {
  switch (fvf & 0x00Eu) {
    case D3DFVF_XYZ:
      return 12u;
    case D3DFVF_XYZRHW:
      return 16u;
    case D3DFVF_XYZB1:
      return 16u;
    case D3DFVF_XYZB2:
      return 20u;
    case D3DFVF_XYZB3:
      return 24u;
    case D3DFVF_XYZB4:
      return 28u;
    case D3DFVF_XYZB5:
      return 32u;
    default:
      return 0u;
  }
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