#pragma once

#include "d3d9.h"

// Numeric D3D9 render states used by shadow/decal-style depth-bias passes.
// Keeping these symbols in the compact WASM D3D9 surface lets production
// renderer code map the original Direct3D contract to WebGL polygonOffset
// without hard-coded state ids at call sites.
#ifndef D3DRS_SLOPESCALEDEPTHBIAS
#define D3DRS_SLOPESCALEDEPTHBIAS static_cast<D3DRENDERSTATETYPE>(175)
#endif

#ifndef D3DRS_DEPTHBIAS
#define D3DRS_DEPTHBIAS static_cast<D3DRENDERSTATETYPE>(195)
#endif
