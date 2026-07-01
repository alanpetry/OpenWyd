#pragma once
#include "d3d9.h"

inline bool D3D9FVFHasSpecular(DWORD fvf) {
  return (fvf & D3DFVF_SPECULAR) != 0u;
}

inline unsigned int D3D9FVFSpecularBytes(DWORD fvf) {
  return D3D9FVFHasSpecular(fvf) ? 4u : 0u;
}

inline unsigned int D3D9FVFColorBytesBeforeTexcoords(DWORD fvf) {
  unsigned int bytes = 0u;
  if ((fvf & D3DFVF_DIFFUSE) != 0u) bytes += 4u;
  bytes += D3D9FVFSpecularBytes(fvf);
  return bytes;
}

inline unsigned int D3D9FVFTexcoordCount(DWORD fvf) {
  return (fvf >> 8) & 0xFu;
}

inline unsigned int D3D9FVFTexcoordBytes(DWORD fvf) {
  return D3D9FVFTexcoordCount(fvf) * 8u;
}
