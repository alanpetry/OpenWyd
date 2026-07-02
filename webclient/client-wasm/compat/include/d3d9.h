#pragma once
#include "unknwn.h"

using D3DCOLOR = DWORD;

enum D3DFORMAT : DWORD {
  D3DFMT_UNKNOWN = 0,
  D3DFMT_R8G8B8 = 20,
  D3DFMT_A8R8G8B8 = 21,
  D3DFMT_X8R8G8B8 = 22,
  D3DFMT_R5G6B5 = 23,
  D3DFMT_X1R5G5B5 = 24,
  D3DFMT_A1R5G5B5 = 25,
  D3DFMT_A4R4G4B4 = 26,
  D3DFMT_R3G3B2 = 27,
  D3DFMT_A8R3G3B2 = 29,
  D3DFMT_X4R4G4B4 = 30,
  D3DFMT_A2B10G10R10 = 31,
  D3DFMT_A8B8G8R8 = 32,
  D3DFMT_A2R10G10B10 = 35,
  D3DFMT_D16 = 80,
  D3DFMT_D24X8 = 77,
  D3DFMT_D24S8 = 75,
  D3DFMT_D24X4S4 = 79,
  D3DFMT_D32 = 71,
  D3DFMT_D15S1 = 73,
  D3DFMT_INDEX16 = 101,
  D3DFMT_INDEX32 = 102,
};

enum D3DPOOL : DWORD {
  D3DPOOL_DEFAULT = 0,
  D3DPOOL_MANAGED = 1,
  D3DPOOL_SYSTEMMEM = 2,
  D3DPOOL_SCRATCH = 3,
};

enum D3DPRIMITIVETYPE : DWORD {
  D3DPT_POINTLIST = 1,
  D3DPT_LINELIST = 2,
  D3DPT_LINESTRIP = 3,
  D3DPT_TRIANGLELIST = 4,
  D3DPT_TRIANGLESTRIP = 5,
  D3DPT_TRIANGLEFAN = 6,
};

enum D3DCULL : DWORD {
  D3DCULL_NONE = 1,
  D3DCULL_CW = 2,
  D3DCULL_CCW = 3,
};

enum D3DCMPFUNC : DWORD {
  D3DCMP_NEVER = 1,
  D3DCMP_LESS = 2,
  D3DCMP_EQUAL = 3,
  D3DCMP_LESSEQUAL = 4,
  D3DCMP_GREATER = 5,
  D3DCMP_NOTEQUAL = 6,
  D3DCMP_GREATEREQUAL = 7,
  D3DCMP_ALWAYS = 8,
};

enum D3DTRANSFORMSTATETYPE : DWORD {
  D3DTS_VIEW = 2,
  D3DTS_PROJECTION = 3,
  D3DTS_TEXTURE0 = 16,
  D3DTS_TEXTURE1 = 17,
  D3DTS_TEXTURE2 = 18,
  D3DTS_TEXTURE3 = 19,
  D3DTS_TEXTURE4 = 20,
  D3DTS_TEXTURE5 = 21,
  D3DTS_TEXTURE6 = 22,
  D3DTS_TEXTURE7 = 23,
  D3DTS_WORLD = 256,
};

#define D3DTS_WORLDMATRIX(index) (D3DTS_WORLD + (index))
#define D3DTS_TEXTURE(index) (D3DTS_TEXTURE0 + (index))

enum D3DRENDERSTATETYPE : DWORD {
  D3DRS_ZENABLE = 7,
  D3DRS_FILLMODE = 8,
  D3DRS_SHADEMODE = 9,
  D3DRS_ZWRITEENABLE = 14,
  D3DRS_ALPHATESTENABLE = 15,
  D3DRS_SRCBLEND = 19,
  D3DRS_DESTBLEND = 20,
  D3DRS_CULLMODE = 22,
  D3DRS_ZFUNC = 23,
  D3DRS_ALPHAREF = 24,
  D3DRS_ALPHAFUNC = 25,
  D3DRS_DITHERENABLE = 26,
  D3DRS_ALPHABLENDENABLE = 27,
  D3DRS_FOGENABLE = 28,
  D3DRS_SPECULARENABLE = 29,
  D3DRS_FOGCOLOR = 34,
  D3DRS_FOGTABLEMODE = 35,
  D3DRS_FOGSTART = 36,
  D3DRS_FOGEND = 37,
  D3DRS_FOGDENSITY = 38,
  D3DRS_RANGEFOGENABLE = 48,
  D3DRS_STENCILENABLE = 52,
  D3DRS_TEXTUREFACTOR = 60,
  D3DRS_WRAP0 = 128,
  D3DRS_CLIPPING = 136,
  D3DRS_LIGHTING = 137,
  D3DRS_AMBIENT = 139,
  D3DRS_FOGVERTEXMODE = 140,
  D3DRS_COLORVERTEX = 141,
  D3DRS_LOCALVIEWER = 142,
  D3DRS_NORMALIZENORMALS = 143,
  D3DRS_DIFFUSEMATERIALSOURCE = 145,
  D3DRS_SPECULARMATERIALSOURCE = 146,
  D3DRS_AMBIENTMATERIALSOURCE = 147,
  D3DRS_EMISSIVEMATERIALSOURCE = 148,
  D3DRS_VERTEXBLEND = 151,
  D3DRS_CLIPPLANEENABLE = 152,
  D3DRS_POINTSIZE = 154,
  D3DRS_MULTISAMPLEANTIALIAS = 161,
  D3DRS_ANTIALIASEDLINEENABLE = 176,
  D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
};

enum D3DFILLMODE : DWORD {
  D3DFILL_POINT = 1,
  D3DFILL_WIREFRAME = 2,
  D3DFILL_SOLID = 3,
};

enum D3DSHADEMODE : DWORD {
  D3DSHADE_FLAT = 1,
  D3DSHADE_GOURAUD = 2,
  D3DSHADE_PHONG = 3,
};

enum D3DTEXTURESTAGESTATETYPE : DWORD {
  D3DTSS_COLOROP = 1,
  D3DTSS_COLORARG1 = 2,
  D3DTSS_COLORARG2 = 3,
  D3DTSS_ALPHAOP = 4,
  D3DTSS_ALPHAARG1 = 5,
  D3DTSS_ALPHAARG2 = 6,
  D3DTSS_TEXCOORDINDEX = 11,
  D3DTSS_TEXTURETRANSFORMFLAGS = 24,
};

enum D3DTEXTUREOP : DWORD {
  D3DTOP_DISABLE = 1,
  D3DTOP_SELECTARG1 = 2,
  D3DTOP_SELECTARG2 = 3,
  D3DTOP_MODULATE = 4,
  D3DTOP_MODULATE2X = 5,
  D3DTOP_MODULATE4X = 6,
  D3DTOP_ADD = 7,
  D3DTOP_ADDSIGNED = 8,
  D3DTOP_ADDSIGNED2X = 9,
  D3DTOP_SUBTRACT = 10,
  D3DTOP_ADDSMOOTH = 11,
  D3DTOP_BLENDDIFFUSEALPHA = 12,
  D3DTOP_BLENDTEXTUREALPHA = 13,
  D3DTOP_BLENDFACTORALPHA = 14,
  D3DTOP_BLENDTEXTUREALPHAPM = 15,
  D3DTOP_BLENDCURRENTALPHA = 16,
  D3DTOP_PREMODULATE = 17,
  D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
  D3DTOP_MODULATECOLOR_ADDALPHA = 19,
  D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
  D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
  D3DTOP_DOTPRODUCT3 = 24,
  D3DTOP_MULTIPLYADD = 25,
  D3DTOP_LERP = 26,
};

enum D3DBLEND : DWORD {
  D3DBLEND_ZERO = 1,
  D3DBLEND_ONE = 2,
  D3DBLEND_SRCCOLOR = 3,
  D3DBLEND_INVSRCCOLOR = 4,
  D3DBLEND_SRCALPHA = 5,
  D3DBLEND_INVSRCALPHA = 6,
  D3DBLEND_DESTALPHA = 7,
  D3DBLEND_INVDESTALPHA = 8,
  D3DBLEND_DESTCOLOR = 9,
  D3DBLEND_INVDESTCOLOR = 10,
};

enum D3DMULTISAMPLE_TYPE : DWORD {
  D3DMULTISAMPLE_NONE = 0,
  D3DMULTISAMPLE_NONMASKABLE = 1,
  D3DMULTISAMPLE_2_SAMPLES = 2,
  D3DMULTISAMPLE_3_SAMPLES = 3,
  D3DMULTISAMPLE_4_SAMPLES = 4,
  D3DMULTISAMPLE_5_SAMPLES = 5,
  D3DMULTISAMPLE_6_SAMPLES = 6,
  D3DMULTISAMPLE_7_SAMPLES = 7,
  D3DMULTISAMPLE_8_SAMPLES = 8,
  D3DMULTISAMPLE_9_SAMPLES = 9,
  D3DMULTISAMPLE_10_SAMPLES = 10,
  D3DMULTISAMPLE_11_SAMPLES = 11,
  D3DMULTISAMPLE_12_SAMPLES = 12,
  D3DMULTISAMPLE_13_SAMPLES = 13,
  D3DMULTISAMPLE_14_SAMPLES = 14,
  D3DMULTISAMPLE_15_SAMPLES = 15,
  D3DMULTISAMPLE_16_SAMPLES = 16,
};

enum D3DDEVTYPE : DWORD {
  D3DDEVTYPE_HAL = 1,
  D3DDEVTYPE_REF = 2,
  D3DDEVTYPE_SW = 3,
};

enum D3DSAMPLERSTATETYPE : DWORD {
  D3DSAMP_ADDRESSU = 1,
  D3DSAMP_ADDRESSV = 2,
  D3DSAMP_ADDRESSW = 3,
  D3DSAMP_BORDERCOLOR = 4,
  D3DSAMP_MAGFILTER = 5,
  D3DSAMP_MINFILTER = 6,
  D3DSAMP_MIPFILTER = 7,
};

enum D3DFOGMODE : DWORD {
  D3DFOG_NONE = 0,
  D3DFOG_EXP = 1,
  D3DFOG_EXP2 = 2,
  D3DFOG_LINEAR = 3,
};

enum D3DLIGHTTYPE : DWORD {
  D3DLIGHT_POINT = 1,
  D3DLIGHT_SPOT = 2,
  D3DLIGHT_DIRECTIONAL = 3,
};

enum D3DMATERIALCOLORSOURCE : DWORD {
  D3DMCS_MATERIAL = 0,
  D3DMCS_COLOR1 = 1,
  D3DMCS_COLOR2 = 2,
};

enum D3DBACKBUFFER_TYPE : DWORD {
  D3DBACKBUFFER_TYPE_MONO = 0,
};

struct D3DVECTOR {
  float x, y, z;
};

struct D3DCOLORVALUE {
  float r, g, b, a;
};

struct D3DMATRIX {
  union {
    struct {
      float _11, _12, _13, _14;
      float _21, _22, _23, _24;
      float _31, _32, _33, _34;
      float _41, _42, _43, _44;
    };
    float m[4][4];
  };
};

struct D3DVIEWPORT9 {
  DWORD X, Y, Width, Height;
  float MinZ, MaxZ;
};

struct D3DMATERIAL9 {
  D3DCOLORVALUE Diffuse, Ambient, Specular, Emissive;
  float Power;
};
using _D3DMATERIAL9 = D3DMATERIAL9;

struct D3DLIGHT9 {
  D3DLIGHTTYPE Type;
  D3DCOLORVALUE Diffuse, Specular, Ambient;
  D3DVECTOR Position, Direction;
  float Range, Falloff, Attenuation0, Attenuation1, Attenuation2, Theta, Phi;
};

struct D3DGAMMARAMP {
  WORD red[256];
  WORD green[256];
  WORD blue[256];
};

struct D3DCAPS9 {
  DWORD DeviceType;
  UINT AdapterOrdinal;
  DWORD Caps;
  DWORD Caps2;
  DWORD Caps3;
  DWORD PresentationIntervals;
  DWORD CursorCaps;
  DWORD DevCaps;
  DWORD PrimitiveMiscCaps;
  DWORD MaxTextureBlendStages;
  DWORD MaxTextureWidth;
  DWORD MaxVertexBlendMatrices;
  DWORD PixelShaderVersion;
  DWORD VertexShaderVersion;
  DWORD dummy[87];
};

struct D3DSURFACE_DESC {
  D3DFORMAT Format;
  DWORD Type;
  DWORD Usage;
  D3DPOOL Pool;
  D3DMULTISAMPLE_TYPE MultiSampleType;
  DWORD MultiSampleQuality;
  UINT Width;
  UINT Height;
};

struct D3DVERTEXBUFFER_DESC {
  D3DFORMAT Format;
  DWORD Type;
  DWORD Usage;
  D3DPOOL Pool;
  UINT Size;
  DWORD FVF;
};

struct D3DINDEXBUFFER_DESC {
  D3DFORMAT Format;
  DWORD Type;
  DWORD Usage;
  D3DPOOL Pool;
  UINT Size;
};

struct D3DDISPLAYMODE {
  UINT Width;
  UINT Height;
  UINT RefreshRate;
  D3DFORMAT Format;
};

struct D3DADAPTER_IDENTIFIER9 {
  char Driver[512];
  char Description[512];
  char DeviceName[32];
  LARGE_INTEGER DriverVersion;
  DWORD VendorId;
  DWORD DeviceId;
  DWORD SubSysId;
  DWORD Revision;
  GUID DeviceIdentifier;
  DWORD WHQLLevel;
};

struct D3DPRESENT_PARAMETERS {
  UINT BackBufferWidth;
  UINT BackBufferHeight;
  D3DFORMAT BackBufferFormat;
  UINT BackBufferCount;
  D3DMULTISAMPLE_TYPE MultiSampleType;
  DWORD MultiSampleQuality;
  DWORD SwapEffect;
  HWND hDeviceWindow;
  BOOL Windowed;
  BOOL EnableAutoDepthStencil;
  D3DFORMAT AutoDepthStencilFormat;
  DWORD Flags;
  UINT FullScreen_RefreshRateInHz;
  UINT PresentationInterval;
};

struct D3DVERTEXELEMENT9 {
  WORD Stream;
  WORD Offset;
  BYTE Type;
  BYTE Method;
  BYTE Usage;
  BYTE UsageIndex;
};

struct D3DLOCKED_RECT {
  INT Pitch;
  void* pBits;
};

struct D3DRECT {
  LONG x1;
  LONG y1;
  LONG x2;
  LONG y2;
};

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;

using LPDIRECT3D9 = IDirect3D9*;
using LPDIRECT3DDEVICE9 = IDirect3DDevice9*;
using LPDIRECT3DSURFACE9 = IDirect3DSurface9*;
using LPDIRECT3DBASETEXTURE9 = IDirect3DBaseTexture9*;
using LPDIRECT3DTEXTURE9 = IDirect3DTexture9*;
using LPDIRECT3DVERTEXBUFFER9 = IDirect3DVertexBuffer9*;
using LPDIRECT3DINDEXBUFFER9 = IDirect3DIndexBuffer9*;
using LPDIRECT3DVERTEXDECLARATION9 = IDirect3DVertexDeclaration9*;
using LPDIRECT3DVERTEXSHADER9 = IDirect3DVertexShader9*;
using LPDIRECT3DPIXELSHADER9 = IDirect3DPixelShader9*;
using LPD3DVERTEXELEMENT9 = D3DVERTEXELEMENT9*;

HRESULT WydD3D9Device_TestCooperativeLevel(IDirect3DDevice9* device);
HRESULT WydD3D9Device_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);
HRESULT WydD3D9Device_Present(
    IDirect3DDevice9* device,
    const RECT* source_rect,
    const RECT* dest_rect,
    HWND dest_window,
    const void* dirty_region);
HRESULT WydD3D9Device_BeginScene(IDirect3DDevice9* device);
HRESULT WydD3D9Device_EndScene(IDirect3DDevice9* device);
HRESULT WydD3D9Device_Clear(
    IDirect3DDevice9* device,
    DWORD count,
    const D3DRECT* rects,
    DWORD flags,
    D3DCOLOR color,
    float z,
    DWORD stencil);
HRESULT WydD3D9Device_GetDeviceCaps(IDirect3DDevice9* device, D3DCAPS9* caps);
HRESULT WydD3D9Device_SetViewport(IDirect3DDevice9* device, const D3DVIEWPORT9* vp);
HRESULT WydD3D9Device_SetMaterial(IDirect3DDevice9* device, const D3DMATERIAL9* material);
HRESULT WydD3D9Device_GetMaterial(IDirect3DDevice9* device, D3DMATERIAL9* material);
HRESULT WydD3D9Device_SetLight(IDirect3DDevice9* device, DWORD index, const D3DLIGHT9* light);
HRESULT WydD3D9Device_GetLight(IDirect3DDevice9* device, DWORD index, D3DLIGHT9* light);
HRESULT WydD3D9Device_LightEnable(IDirect3DDevice9* device, DWORD index, BOOL enable);
HRESULT WydD3D9Device_GetLightEnable(IDirect3DDevice9* device, DWORD index, BOOL* enable);
HRESULT WydD3D9Device_SetRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value);
HRESULT WydD3D9Device_SetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* mat);
HRESULT WydD3D9Device_SetTexture(IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture);
HRESULT WydD3D9Device_SetTextureStageState(
    IDirect3DDevice9* device,
    DWORD stage,
    D3DTEXTURESTAGESTATETYPE type,
    DWORD value);
HRESULT WydD3D9Device_SetSamplerState(
    IDirect3DDevice9* device,
    DWORD sampler,
    D3DSAMPLERSTATETYPE type,
    DWORD value);
HRESULT WydD3D9Device_SetFVF(IDirect3DDevice9* device, DWORD fvf);
HRESULT WydD3D9Device_SetStreamSource(
    IDirect3DDevice9* device,
    UINT stream,
    IDirect3DVertexBuffer9* vb,
    UINT offset,
    UINT stride);
HRESULT WydD3D9Device_SetIndices(IDirect3DDevice9* device, IDirect3DIndexBuffer9* ib);
HRESULT WydD3D9Device_CreateVertexDeclaration(
    IDirect3DDevice9* device,
    const D3DVERTEXELEMENT9* vertex_elements,
    IDirect3DVertexDeclaration9** pp_decl);
HRESULT WydD3D9Device_SetVertexDeclaration(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* decl);
HRESULT WydD3D9Device_CreateVertexShader(
    IDirect3DDevice9* device,
    const DWORD* function_code,
    IDirect3DVertexShader9** pp_shader);
HRESULT WydD3D9Device_SetVertexShader(IDirect3DDevice9* device, IDirect3DVertexShader9* shader);
HRESULT WydD3D9Device_SetVertexShaderConstantF(
    IDirect3DDevice9* device,
    UINT start_register,
    const float* constants,
    UINT vector4f_count);
HRESULT WydD3D9Device_CreatePixelShader(
    IDirect3DDevice9* device,
    const DWORD* function_code,
    IDirect3DPixelShader9** pp_shader);
HRESULT WydD3D9Device_SetPixelShader(IDirect3DDevice9* device, IDirect3DPixelShader9* shader);
HRESULT WydD3D9Device_DrawPrimitiveUP(
    IDirect3DDevice9* device,
    D3DPRIMITIVETYPE primitive_type,
    UINT primitive_count,
    const void* vertex_stream_zero_data,
    UINT vertex_stream_zero_stride);
HRESULT WydD3D9Device_DrawIndexedPrimitiveUP(
    IDirect3DDevice9* device,
    D3DPRIMITIVETYPE primitive_type,
    UINT min_vertex_index,
    UINT num_vertices,
    UINT primitive_count,
    const void* index_data,
    D3DFORMAT index_data_format,
    const void* vertex_stream_zero_data,
    UINT vertex_stream_zero_stride);
HRESULT WydD3D9Device_DrawIndexedPrimitive(
    IDirect3DDevice9* device,
    D3DPRIMITIVETYPE primitive_type,
    INT base_vertex_index,
    UINT min_vertex_index,
    UINT num_vertices,
    UINT start_index,
    UINT primitive_count);
HRESULT WydD3D9Device_CreateVertexBuffer(
    IDirect3DDevice9* device,
    UINT length,
    DWORD usage,
    DWORD fvf,
    D3DPOOL pool,
    IDirect3DVertexBuffer9** pp_vb,
    void* shared_handle);
HRESULT WydD3D9Device_CreateIndexBuffer(
    IDirect3DDevice9* device,
    UINT length,
    DWORD usage,
    D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DIndexBuffer9** pp_ib,
    void* shared_handle);
HRESULT WydD3D9Device_GetRenderTarget(IDirect3DDevice9* device, DWORD render_target_index, IDirect3DSurface9** pp_surface);
HRESULT WydD3D9Device_GetDepthStencilSurface(IDirect3DDevice9* device, IDirect3DSurface9** pp_z_stencil_surface);
HRESULT WydD3D9Device_CreateRenderTarget(
    IDirect3DDevice9* device,
    UINT width,
    UINT height,
    D3DFORMAT format,
    D3DMULTISAMPLE_TYPE multisample,
    DWORD multisample_quality,
    BOOL lockable,
    IDirect3DSurface9** pp_surface,
    void* shared_handle);
HRESULT WydD3D9Device_ColorFill(IDirect3DDevice9* device, IDirect3DSurface9* surface, const RECT* rect, D3DCOLOR color);

HRESULT WydD3D9Surface_GetDesc(IDirect3DSurface9* surface, D3DSURFACE_DESC* desc);
HRESULT WydD3D9Surface_LockRect(IDirect3DSurface9* surface, D3DLOCKED_RECT* locked_rect, const RECT* rect, DWORD flags);
HRESULT WydD3D9Surface_UnlockRect(IDirect3DSurface9* surface);

HRESULT WydD3D9Texture_GetLevelDesc(IDirect3DBaseTexture9* texture, UINT level, D3DSURFACE_DESC* desc);
HRESULT WydD3D9Texture_GetSurfaceLevel(IDirect3DTexture9* texture, UINT level, IDirect3DSurface9** pp_surface_level);
HRESULT WydD3D9Texture_LockRect(
    IDirect3DTexture9* texture,
    UINT level,
    D3DLOCKED_RECT* locked_rect,
    const RECT* rect,
    DWORD flags);
HRESULT WydD3D9Texture_UnlockRect(IDirect3DTexture9* texture, UINT level);

HRESULT WydD3D9VertexBuffer_GetDesc(IDirect3DVertexBuffer9* vb, D3DVERTEXBUFFER_DESC* desc);
HRESULT WydD3D9VertexBuffer_Lock(
    IDirect3DVertexBuffer9* vb,
    UINT offset_to_lock,
    UINT size_to_lock,
    void** ppb_data,
    DWORD flags);
HRESULT WydD3D9VertexBuffer_Unlock(IDirect3DVertexBuffer9* vb);

HRESULT WydD3D9IndexBuffer_GetDesc(IDirect3DIndexBuffer9* ib, D3DINDEXBUFFER_DESC* desc);
HRESULT WydD3D9IndexBuffer_Lock(
    IDirect3DIndexBuffer9* ib,
    UINT offset_to_lock,
    UINT size_to_lock,
    void** ppb_data,
    DWORD flags);
HRESULT WydD3D9IndexBuffer_Unlock(IDirect3DIndexBuffer9* ib);

struct IDirect3DSurface9 : public IUnknown {
  HRESULT GetDesc(D3DSURFACE_DESC* pDesc) { return WydD3D9Surface_GetDesc(this, pDesc); }
  HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) {
    return WydD3D9Surface_LockRect(this, pLockedRect, pRect, Flags);
  }
  HRESULT UnlockRect() { return WydD3D9Surface_UnlockRect(this); }
};

struct IDirect3DBaseTexture9 : public IUnknown {
  HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) {
    return WydD3D9Texture_GetLevelDesc(this, Level, pDesc);
  }
};

struct IDirect3DTexture9 : public IDirect3DBaseTexture9 {
  HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
    return WydD3D9Texture_GetSurfaceLevel(this, Level, ppSurfaceLevel);
  }
  HRESULT LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) {
    return WydD3D9Texture_LockRect(this, Level, pLockedRect, pRect, Flags);
  }
  HRESULT UnlockRect(UINT Level) { return WydD3D9Texture_UnlockRect(this, Level); }
};

struct IDirect3DVertexBuffer9 : public IUnknown {
  HRESULT GetDesc(D3DVERTEXBUFFER_DESC* pDesc) { return WydD3D9VertexBuffer_GetDesc(this, pDesc); }
  HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    return WydD3D9VertexBuffer_Lock(this, OffsetToLock, SizeToLock, ppbData, Flags);
  }
  HRESULT Unlock() { return WydD3D9VertexBuffer_Unlock(this); }
};

struct IDirect3DIndexBuffer9 : public IUnknown {
  HRESULT GetDesc(D3DINDEXBUFFER_DESC* pDesc) { return WydD3D9IndexBuffer_GetDesc(this, pDesc); }
  HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    return WydD3D9IndexBuffer_Lock(this, OffsetToLock, SizeToLock, ppbData, Flags);
  }
  HRESULT Unlock() { return WydD3D9IndexBuffer_Unlock(this); }
};

struct IDirect3DVertexDeclaration9 : public IUnknown {};
struct IDirect3DVertexShader9 : public IUnknown {};
struct IDirect3DPixelShader9 : public IUnknown {};

struct IDirect3DDevice9 : public IUnknown {
  HRESULT TestCooperativeLevel() { return WydD3D9Device_TestCooperativeLevel(this); }
  template <typename... Args>
  UINT GetAvailableTextureMem(Args...) { return 256u * 1024u * 1024u; }
  HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) {
    return WydD3D9Device_Reset(this, pPresentationParameters);
  }
  HRESULT Present(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const void* pDirtyRegion) {
    return WydD3D9Device_Present(this, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
  }
  HRESULT BeginScene() { return WydD3D9Device_BeginScene(this); }
  HRESULT EndScene() { return WydD3D9Device_EndScene(this); }
  HRESULT Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
    return WydD3D9Device_Clear(this, Count, pRects, Flags, Color, Z, Stencil);
  }
  HRESULT GetDeviceCaps(D3DCAPS9* pCaps) { return WydD3D9Device_GetDeviceCaps(this, pCaps); }
  template <typename... Args>
  HRESULT GetBackBuffer(Args...) { return E_NOTIMPL; }
  HRESULT GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) {
    return WydD3D9Device_GetDepthStencilSurface(this, ppZStencilSurface);
  }
  template <typename TMatrix>
  HRESULT SetTransform(DWORD State, const TMatrix* pMatrix) {
    return WydD3D9Device_SetTransform(
        this, static_cast<D3DTRANSFORMSTATETYPE>(State), reinterpret_cast<const D3DMATRIX*>(pMatrix));
  }
  HRESULT GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) {
    return WydD3D9Device_GetRenderTarget(this, RenderTargetIndex, ppRenderTarget);
  }
  HRESULT SetViewport(const D3DVIEWPORT9* pViewport) { return WydD3D9Device_SetViewport(this, pViewport); }
  HRESULT SetMaterial(const D3DMATERIAL9* pMaterial) { return WydD3D9Device_SetMaterial(this, pMaterial); }
  HRESULT GetMaterial(D3DMATERIAL9* pMaterial) { return WydD3D9Device_GetMaterial(this, pMaterial); }
  HRESULT SetLight(DWORD Index, const D3DLIGHT9* pLight) { return WydD3D9Device_SetLight(this, Index, pLight); }
  HRESULT GetLight(DWORD Index, D3DLIGHT9* pLight) { return WydD3D9Device_GetLight(this, Index, pLight); }
  HRESULT LightEnable(DWORD Index, BOOL Enable) { return WydD3D9Device_LightEnable(this, Index, Enable); }
  HRESULT GetLightEnable(DWORD Index, BOOL* pEnable) { return WydD3D9Device_GetLightEnable(this, Index, pEnable); }
  HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) { return WydD3D9Device_SetRenderState(this, State, Value); }
  HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) {
    return WydD3D9Device_SetTexture(this, Stage, pTexture);
  }
  HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
    return WydD3D9Device_SetTextureStageState(this, Stage, Type, Value);
  }
  HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) {
    return WydD3D9Device_SetSamplerState(this, Sampler, Type, Value);
  }
  HRESULT SetFVF(DWORD FVF) {
    HRESULT hr = WydD3D9Device_SetFVF(this, FVF);
    if (hr != S_OK) return hr;
    return WydD3D9Device_SetPixelShader(this, nullptr);
  }
  HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) {
    return WydD3D9Device_SetStreamSource(this, StreamNumber, pStreamData, OffsetInBytes, Stride);
  }
  HRESULT SetIndices(IDirect3DIndexBuffer9* pIndexData) { return WydD3D9Device_SetIndices(this, pIndexData); }
  HRESULT DrawPrimitiveUP(
      D3DPRIMITIVETYPE PrimitiveType,
      UINT PrimitiveCount,
      const void* pVertexStreamZeroData,
      UINT VertexStreamZeroStride) {
    return WydD3D9Device_DrawPrimitiveUP(this, PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
  }
  HRESULT DrawIndexedPrimitiveUP(
      D3DPRIMITIVETYPE PrimitiveType,
      UINT MinVertexIndex,
      UINT NumVertices,
      UINT PrimitiveCount,
      const void* pIndexData,
      D3DFORMAT IndexDataFormat,
      const void* pVertexStreamZeroData,
      UINT VertexStreamZeroStride) {
    return WydD3D9Device_DrawIndexedPrimitiveUP(
        this,
        PrimitiveType,
        MinVertexIndex,
        NumVertices,
        PrimitiveCount,
        pIndexData,
        IndexDataFormat,
        pVertexStreamZeroData,
        VertexStreamZeroStride);
  }
  HRESULT DrawIndexedPrimitive(
      D3DPRIMITIVETYPE Type,
      INT BaseVertexIndex,
      UINT MinVertexIndex,
      UINT NumVertices,
      UINT StartIndex,
      UINT PrimitiveCount) {
    return WydD3D9Device_DrawIndexedPrimitive(
        this, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
  }
  HRESULT CreateVertexBuffer(
      UINT Length,
      DWORD Usage,
      DWORD FVF,
      D3DPOOL Pool,
      IDirect3DVertexBuffer9** ppVertexBuffer,
      void* pSharedHandle) {
    return WydD3D9Device_CreateVertexBuffer(this, Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
  }
  HRESULT CreateIndexBuffer(
      UINT Length,
      DWORD Usage,
      D3DFORMAT Format,
      D3DPOOL Pool,
      IDirect3DIndexBuffer9** ppIndexBuffer,
      void* pSharedHandle) {
    return WydD3D9Device_CreateIndexBuffer(this, Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
  }
  HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) {
    return WydD3D9Device_CreateVertexDeclaration(this, pVertexElements, ppDecl);
  }
  HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) {
    return WydD3D9Device_SetVertexDeclaration(this, pDecl);
  }
  HRESULT CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) {
    return WydD3D9Device_CreateVertexShader(this, pFunction, ppShader);
  }
  HRESULT SetVertexShader(IDirect3DVertexShader9* pShader) {
    return WydD3D9Device_SetVertexShader(this, pShader);
  }
  HRESULT SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
    return WydD3D9Device_SetVertexShaderConstantF(this, StartRegister, pConstantData, Vector4fCount);
  }
  HRESULT CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) {
    return WydD3D9Device_CreatePixelShader(this, pFunction, ppShader);
  }
  HRESULT SetPixelShader(IDirect3DPixelShader9* pShader) {
    return WydD3D9Device_SetPixelShader(this, pShader);
  }
  HRESULT CreateRenderTarget(
      UINT Width,
      UINT Height,
      D3DFORMAT Format,
      D3DMULTISAMPLE_TYPE MultiSample,
      DWORD MultisampleQuality,
      BOOL Lockable,
      IDirect3DSurface9** ppSurface,
      void* pSharedHandle) {
    return WydD3D9Device_CreateRenderTarget(
        this, Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
  }
  HRESULT ColorFill(IDirect3DSurface9* pSurface, const RECT* pRect, D3DCOLOR color) {
    return WydD3D9Device_ColorFill(this, pSurface, pRect, color);
  }
  template <typename... Args>
  HRESULT SetSoftwareVertexProcessing(Args...) { return S_OK; }
  template <typename... Args>
  void SetGammaRamp(Args...) {}
  template <typename... Args>
  void GetGammaRamp(Args...) {}
};

struct IDirect3D9 : public IUnknown {
  template <typename... Args>
  UINT GetAdapterCount(Args...) { return 0; }
  template <typename... Args>
  UINT GetAdapterModeCount(Args...) { return 0; }
  template <typename... Args>
  HRESULT EnumAdapterModes(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT GetAdapterDisplayMode(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT CheckDeviceType(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT CheckDeviceFormat(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT CheckDepthStencilMatch(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT CheckDeviceMultiSampleType(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT GetDeviceCaps(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT GetAdapterIdentifier(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT CreateDevice(Args...) { return E_NOTIMPL; }
};

#define D3D_SDK_VERSION 32
#define D3D9b_SDK_VERSION D3D_SDK_VERSION
#define D3DADAPTER_DEFAULT 0

#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x00000080L
#define D3DCREATE_PUREDEVICE 0x00000010L

#define D3DPRESENT_INTERVAL_DEFAULT 0x00000000L
#define D3DPRESENT_INTERVAL_ONE 0x00000001L
#define D3DPRESENT_INTERVAL_TWO 0x00000002L
#define D3DPRESENT_INTERVAL_THREE 0x00000004L
#define D3DPRESENT_INTERVAL_FOUR 0x00000008L
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000L

#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER 0x00000001L
#define D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL 0x00000002L

#define D3DSWAPEFFECT_DISCARD 1

#define D3DDEVCAPS_EXECUTESYSTEMMEMORY 0x00000010L
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000L
#define D3DDEVCAPS_PUREDEVICE 0x00100000L

#define D3DPMISCCAPS_NULLREFERENCE 0x00000004L

#define D3DUSAGE_DEPTHSTENCIL 0x00000002L
#define D3DUSAGE_WRITEONLY 0x00000008L
#define D3DUSAGE_DYNAMIC 0x00000200L

#define D3DRTYPE_SURFACE 1

#define D3DCLEAR_TARGET 0x00000001L
#define D3DCLEAR_ZBUFFER 0x00000002L

#define D3DERR_INVALIDCALL ((HRESULT)0x8876086CL)
#define D3DERR_DEVICELOST ((HRESULT)0x88760868L)
#define D3DERR_DEVICENOTRESET ((HRESULT)0x88760869L)
#define D3DERR_OUTOFVIDEOMEMORY ((HRESULT)0x8876017CL)

#define D3DFVF_XYZ 0x002
#define D3DFVF_XYZRHW 0x004
#define D3DFVF_XYZB1 0x006
#define D3DFVF_XYZB2 0x008
#define D3DFVF_XYZB3 0x00A
#define D3DFVF_XYZB4 0x00C
#define D3DFVF_XYZB5 0x00E
#define D3DFVF_NORMAL 0x010
#define D3DFVF_DIFFUSE 0x040
#define D3DFVF_TEX1 0x100
#define D3DFVF_TEX2 0x200
#define D3DFVF_LASTBETA_UBYTE4 0x1000
#define D3DFVF_LASTBETA_D3DCOLOR 0x8000

#define D3DZB_FALSE 0
#define D3DZB_TRUE 1

#define D3DTA_DIFFUSE 0x00000000
#define D3DTA_CURRENT 0x00000001
#define D3DTA_TEXTURE 0x00000002
#define D3DTA_TFACTOR 0x00000003
#define D3DTA_COMPLEMENT 0x00000010
#define D3DTA_ALPHAREPLICATE 0x00000020

#define D3DTTFF_DISABLE 0x00000000
#define D3DTTFF_COUNT1 0x00000001
#define D3DTTFF_COUNT2 0x00000002
#define D3DTTFF_COUNT3 0x00000003
#define D3DTTFF_COUNT4 0x00000004
#define D3DTTFF_PROJECTED 0x00000100

#define D3DTSS_TCI_PASSTHRU 0x00000000
#define D3DTSS_TCI_CAMERASPACENORMAL 0x00010000
#define D3DTSS_TCI_CAMERASPACEPOSITION 0x00020000
#define D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR 0x00030000

extern "C" {
IDirect3D9* Direct3DCreate9(UINT SDKVersion);
IDirect3DDevice9* WydCreateDummyD3DDevice();
}
