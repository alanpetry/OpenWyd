#pragma once
#include "d3d9.h"

struct D3DXVECTOR2 {
  float x, y;
  D3DXVECTOR2() : x(0), y(0) {}
  D3DXVECTOR2(float ix, float iy) : x(ix), y(iy) {}
};

struct D3DXVECTOR3 {
  float x, y, z;
  D3DXVECTOR3() : x(0), y(0), z(0) {}
  D3DXVECTOR3(float ix, float iy, float iz) : x(ix), y(iy), z(iz) {}
  D3DXVECTOR3 operator+(const D3DXVECTOR3& r) const { return {x + r.x, y + r.y, z + r.z}; }
  D3DXVECTOR3 operator-(const D3DXVECTOR3& r) const { return {x - r.x, y - r.y, z - r.z}; }
  D3DXVECTOR3 operator*(float s) const { return {x * s, y * s, z * s}; }
  D3DXVECTOR3& operator+=(const D3DXVECTOR3& r) { x += r.x; y += r.y; z += r.z; return *this; }
  D3DXVECTOR3& operator-=(const D3DXVECTOR3& r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
  operator D3DVECTOR() const { return D3DVECTOR{x, y, z}; }
};

struct D3DXVECTOR4 {
  float x, y, z, w;
  D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
  D3DXVECTOR4(float ix, float iy, float iz, float iw) : x(ix), y(iy), z(iz), w(iw) {}
};

struct D3DXQUATERNION {
  float x, y, z, w;
  D3DXQUATERNION() : x(0), y(0), z(0), w(1) {}
  D3DXQUATERNION(float ix, float iy, float iz, float iw) : x(ix), y(iy), z(iz), w(iw) {}
};

struct D3DXCOLOR {
  float r, g, b, a;
  D3DXCOLOR() : r(0), g(0), b(0), a(0) {}
  D3DXCOLOR(float ir, float ig, float ib, float ia) : r(ir), g(ig), b(ib), a(ia) {}
  D3DXCOLOR(float ir, float ig, float ib) : r(ir), g(ig), b(ib), a(1.0f) {}
  D3DXCOLOR(D3DCOLOR c)
      : r(((c >> 16) & 0xFF) / 255.0f),
        g(((c >> 8) & 0xFF) / 255.0f),
        b((c & 0xFF) / 255.0f),
        a(((c >> 24) & 0xFF) / 255.0f) {}
  D3DXCOLOR(const D3DCOLORVALUE& v) : r(v.r), g(v.g), b(v.b), a(v.a) {}
  operator D3DCOLORVALUE() const { return D3DCOLORVALUE{r, g, b, a}; }
  D3DXCOLOR& operator+=(const D3DXCOLOR& o) {
    r += o.r;
    g += o.g;
    b += o.b;
    a += o.a;
    return *this;
  }
  D3DXCOLOR& operator+=(const D3DCOLORVALUE& o) {
    r += o.r;
    g += o.g;
    b += o.b;
    a += o.a;
    return *this;
  }
};

struct D3DXMATRIX {
  union {
    struct {
      float _11, _12, _13, _14;
      float _21, _22, _23, _24;
      float _31, _32, _33, _34;
      float _41, _42, _43, _44;
    };
    float m[4][4];
  };

  D3DXMATRIX()
      : _11(1), _12(0), _13(0), _14(0),
        _21(0), _22(1), _23(0), _24(0),
        _31(0), _32(0), _33(1), _34(0),
        _41(0), _42(0), _43(0), _44(1) {}

  D3DXMATRIX operator*(const D3DXMATRIX& rhs) const {
    D3DXMATRIX out;
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        out.m[i][j] = 0.0f;
        for (int k = 0; k < 4; ++k) {
          out.m[i][j] += m[i][k] * rhs.m[k][j];
        }
      }
    }
    return out;
  }

  D3DXMATRIX& operator*=(const D3DXMATRIX& rhs) {
    *this = (*this) * rhs;
    return *this;
  }
};

using D3DXMATRIXA16 = D3DXMATRIX;

struct D3DXATTRIBUTERANGE {
  DWORD AttribId;
  DWORD FaceStart;
  DWORD FaceCount;
  DWORD VertexStart;
  DWORD VertexCount;
};

struct ID3DXBuffer : public IUnknown {
  void* data = nullptr;
  DWORD size = 0;
  void* GetBufferPointer() { return data; }
  DWORD GetBufferSize() { return size; }
};
struct ID3DXSprite : public IUnknown {
  virtual HRESULT GetDevice(IDirect3DDevice9**) { return E_NOTIMPL; }
  virtual HRESULT Begin(DWORD = 0) { return E_NOTIMPL; }
  virtual HRESULT Draw(
      IDirect3DTexture9*,
      const RECT*,
      const D3DXVECTOR2*,
      const D3DXVECTOR2*,
      float,
      const D3DXVECTOR2*,
      D3DCOLOR) {
    return E_NOTIMPL;
  }
  virtual HRESULT DrawTransform(IDirect3DTexture9*, const RECT*, const D3DXMATRIX*, D3DCOLOR) {
    return E_NOTIMPL;
  }
  virtual HRESULT End() { return E_NOTIMPL; }
  virtual HRESULT OnLostDevice() { return E_NOTIMPL; }
  virtual HRESULT OnResetDevice() { return E_NOTIMPL; }
};
struct ID3DXFont : public IUnknown {
  template <typename... Args>
  HRESULT OnResetDevice(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT OnLostDevice(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT DrawTextA(Args...) { return E_NOTIMPL; }
};

using LPD3DXMATRIX = D3DXMATRIX*;
using LPD3DXVECTOR3 = D3DXVECTOR3*;
using LPD3DXVECTOR4 = D3DXVECTOR4*;
using LPD3DXQUATERNION = D3DXQUATERNION*;
using LPD3DXSPRITE = ID3DXSprite*;
using LPD3DXFONT = ID3DXFont*;
using LPD3DXCOLOR = D3DXCOLOR*;
using LPD3DXBUFFER = ID3DXBuffer*;

enum D3DXIMAGE_FILEFORMAT : DWORD {
  D3DXIFF_BMP = 0,
};

#ifndef D3DX_PI
#define D3DX_PI 3.14159265358979323846f
#endif
#ifndef D3DXToRadian
#define D3DXToRadian(degree) ((degree) * (D3DX_PI / 180.0f))
#endif

extern "C" {
D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX*);
D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX*, const D3DXMATRIX*, const D3DXMATRIX*);
D3DXMATRIX* D3DXMatrixMultiplyTranspose(D3DXMATRIX*, const D3DXMATRIX*, const D3DXMATRIX*);
D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX*, float, float, float);
D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX*, float, float, float);
D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX*, const D3DXMATRIX*);
D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX*, float*, const D3DXMATRIX*);
D3DXMATRIX* D3DXMatrixLookAtLH(D3DXMATRIX*, const D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXVECTOR3*);
D3DXMATRIX* D3DXMatrixPerspectiveFovLH(D3DXMATRIX*, float, float, float, float);
D3DXMATRIX* D3DXMatrixRotationX(D3DXMATRIX*, float);
D3DXMATRIX* D3DXMatrixRotationY(D3DXMATRIX*, float);
D3DXMATRIX* D3DXMatrixRotationZ(D3DXMATRIX*, float);
D3DXMATRIX* D3DXMatrixRotationAxis(D3DXMATRIX*, const D3DXVECTOR3*, float);
D3DXMATRIX* D3DXMatrixRotationYawPitchRoll(D3DXMATRIX*, float, float, float);
D3DXMATRIX* D3DXMatrixRotationQuaternion(D3DXMATRIX*, const D3DXQUATERNION*);

D3DXVECTOR3* D3DXVec3TransformCoord(D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXMATRIX*);
D3DXVECTOR4* D3DXVec3Transform(D3DXVECTOR4*, const D3DXVECTOR3*, const D3DXMATRIX*);
D3DXVECTOR3* D3DXVec3Normalize(D3DXVECTOR3*, const D3DXVECTOR3*);
D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXVECTOR3*);
D3DXVECTOR3* D3DXVec3Lerp(D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXVECTOR3*, float);
float D3DXVec3Length(const D3DXVECTOR3*);
float D3DXVec3Dot(const D3DXVECTOR3*, const D3DXVECTOR3*);
D3DXVECTOR2* D3DXVec2Normalize(D3DXVECTOR2*, const D3DXVECTOR2*);
float D3DXVec2Length(const D3DXVECTOR2*);

D3DXQUATERNION* D3DXQuaternionIdentity(D3DXQUATERNION*);
D3DXQUATERNION* D3DXQuaternionRotationMatrix(D3DXQUATERNION*, const D3DXMATRIX*);
D3DXQUATERNION* D3DXQuaternionSlerp(D3DXQUATERNION*, const D3DXQUATERNION*, const D3DXQUATERNION*, float);

BOOL D3DXIntersectTri(const D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXVECTOR3*, float*, float*, float*);
D3DXVECTOR3* D3DXVec3Project(D3DXVECTOR3*, const D3DXVECTOR3*, const D3DVIEWPORT9*, const D3DXMATRIX*, const D3DXMATRIX*, const D3DXMATRIX*);
D3DXCOLOR* D3DXColorLerp(D3DXCOLOR*, const D3DXCOLOR*, const D3DXCOLOR*, float);
D3DXCOLOR* D3DXColorModulate(D3DXCOLOR*, const D3DXCOLOR*, const D3DXCOLOR*);

HRESULT D3DXCreateTexture(
    IDirect3DDevice9* pDevice,
    UINT Width,
    UINT Height,
    UINT MipLevels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    IDirect3DTexture9** ppTexture);
HRESULT D3DXCreateTextureFromFileInMemoryEx(
    IDirect3DDevice9* pDevice,
    LPCVOID pSrcData,
    UINT SrcDataSize,
    UINT Width,
    UINT Height,
    UINT MipLevels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    DWORD Filter,
    DWORD MipFilter,
    D3DCOLOR ColorKey,
    void* pSrcInfo,
    PALETTEENTRY* pPalette,
    IDirect3DTexture9** ppTexture);
HRESULT D3DXCreateBuffer(DWORD NumBytes, ID3DXBuffer** ppBuffer);
HRESULT D3DXCreateSprite(IDirect3DDevice9* pDevice, ID3DXSprite** ppSprite);
HRESULT D3DXSaveSurfaceToFile(
    LPCSTR pDestFile,
    D3DXIMAGE_FILEFORMAT DestFormat,
    IDirect3DSurface9* pSrcSurface,
    const PALETTEENTRY* pSrcPalette,
    const RECT* pSrcRect);
HRESULT D3DXCreateFont(...);
}
