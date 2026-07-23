#pragma once

#ifndef __EMSCRIPTEN_PORT__
#define __EMSCRIPTEN_PORT__ 1
#endif

#ifndef WINAPI
#define WINAPI
#endif
#ifndef APIENTRY
#define APIENTRY WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef STDMETHODVCALLTYPE
#define STDMETHODVCALLTYPE
#endif
#ifndef STDAPICALLTYPE
#define STDAPICALLTYPE
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __thiscall
#define __thiscall
#endif
#ifndef __forceinline
#define __forceinline inline
#endif

#ifndef __declspec
#define __declspec(x)
#endif

#ifndef DECLSPEC_ALIGN
#define DECLSPEC_ALIGN(x)
#endif

#ifndef interface
#define interface struct
#endif

#ifndef MIDL_INTERFACE
#define MIDL_INTERFACE(x) struct
#endif

#ifndef DECLARE_INTERFACE
#define DECLARE_INTERFACE(iface) struct iface
#endif
#ifndef DECLARE_INTERFACE_
#define DECLARE_INTERFACE_(iface, baseiface) struct iface : public baseiface
#endif

#ifndef PURE
#define PURE = 0
#endif

#ifndef THIS
#define THIS
#endif
#ifndef THIS_
#define THIS_
#endif

#ifndef STDMETHOD
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#endif
#ifndef STDMETHOD_
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#endif
#ifndef STDMETHODIMP
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#endif
#ifndef STDMETHODIMP_
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#endif

#ifndef _In_
#define _In_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif
#ifndef _Ret_notnull_
#define _Ret_notnull_
#endif
#ifndef _Check_return_
#define _Check_return_
#endif
#ifndef _Out_writes_
#define _Out_writes_(x)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(x)
#endif
#ifndef _In_reads_
#define _In_reads_(x)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(x)
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _Outptr_
#define _Outptr_
#endif
#ifndef _Outptr_result_maybenull_
#define _Outptr_result_maybenull_
#endif
#ifndef _COM_Outptr_
#define _COM_Outptr_
#endif

#ifndef __analysis_assume
#define __analysis_assume(x)
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef _countof
#define _countof(a) ARRAYSIZE(a)
#endif

#ifndef __int8
#define __int8 char
#endif
#ifndef __int16
#define __int16 short
#endif
#ifndef __int32
#define __int32 int
#endif
#ifndef __int64
#define __int64 long long
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <algorithm>
#include <cstdlib>
#include <alloca.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/console.h>
#endif

using BYTE = std::uint8_t;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using ULONG = unsigned long;
using LONG = long;
using BOOL = int;
using FLOAT = float;
using INT = int;
using WCHAR = wchar_t;
using CHAR = char;

#ifndef D3DFVF_POSITION_MASK
#define D3DFVF_POSITION_MASK 0x400Eu
#endif
#ifndef D3DFVF_XYZ
#define D3DFVF_XYZ 0x0002u
#endif
#ifndef D3DFVF_XYZRHW
#define D3DFVF_XYZRHW 0x0004u
#endif
#ifndef D3DFVF_XYZB1
#define D3DFVF_XYZB1 0x0006u
#endif
#ifndef D3DFVF_XYZB2
#define D3DFVF_XYZB2 0x0008u
#endif
#ifndef D3DFVF_XYZB3
#define D3DFVF_XYZB3 0x000Au
#endif
#ifndef D3DFVF_XYZB4
#define D3DFVF_XYZB4 0x000Cu
#endif
#ifndef D3DFVF_XYZB5
#define D3DFVF_XYZB5 0x000Eu
#endif
#ifndef D3DFVF_NORMAL
#define D3DFVF_NORMAL 0x0010u
#endif
#ifndef D3DFVF_DIFFUSE
#define D3DFVF_DIFFUSE 0x0040u
#endif
#ifndef D3DFVF_LASTBETA_UBYTE4
#define D3DFVF_LASTBETA_UBYTE4 0x1000u
#endif
#ifndef D3DFVF_LASTBETA_D3DCOLOR
#define D3DFVF_LASTBETA_D3DCOLOR 0x8000u
#endif
#ifndef D3DFVF_TEXCOUNT_SHIFT
#define D3DFVF_TEXCOUNT_SHIFT 8
#endif
#ifndef D3DFVF_TEXCOUNT_MASK
#define D3DFVF_TEXCOUNT_MASK 0xF00
#endif
#ifndef D3DFVF_TEXCOORDSIZE1
#define D3DFVF_TEXCOORDSIZE1(coord_index) (3u << ((coord_index) * 2u + 16u))
#endif
#ifndef D3DFVF_TEXCOORDSIZE2
#define D3DFVF_TEXCOORDSIZE2(coord_index) (0u << ((coord_index) * 2u + 16u))
#endif
#ifndef D3DFVF_TEXCOORDSIZE3
#define D3DFVF_TEXCOORDSIZE3(coord_index) (1u << ((coord_index) * 2u + 16u))
#endif
#ifndef D3DFVF_TEXCOORDSIZE4
#define D3DFVF_TEXCOORDSIZE4(coord_index) (2u << ((coord_index) * 2u + 16u))
#endif

inline UINT D3DFVFTexcoordCount(DWORD fvf) {
  return (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
}

inline UINT D3DFVFPositionBlendCount(DWORD fvf) {
  switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZB1: return 1u;
    case D3DFVF_XYZB2: return 2u;
    case D3DFVF_XYZB3: return 3u;
    case D3DFVF_XYZB4: return 4u;
    case D3DFVF_XYZB5: return 5u;
    default: return 0u;
  }
}

inline UINT D3DFVFPositionBytes(DWORD fvf) {
  if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) return 16u;
  UINT bytes = 12u;
  UINT blend_count = D3DFVFPositionBlendCount(fvf);
  if (blend_count > 0u) {
    if ((fvf & (D3DFVF_LASTBETA_UBYTE4 | D3DFVF_LASTBETA_D3DCOLOR)) != 0u) {
      if (blend_count > 0u) --blend_count;
      bytes += 4u;
    }
    bytes += blend_count * 4u;
  }
  return bytes;
}

inline UINT D3DFVFTexcoordBaseOffsetBytes(DWORD fvf) {
  UINT offset = D3DFVFPositionBytes(fvf);
  if ((fvf & D3DFVF_NORMAL) != 0u) offset += 12u;
  if ((fvf & D3DFVF_DIFFUSE) != 0u) offset += 4u;
  return offset;
}

inline UINT D3DFVFTexcoordComponentCount(DWORD fvf, UINT coord_index) {
  const DWORD encoded = (fvf >> (coord_index * 2u + 16u)) & 0x3u;
  switch (encoded) {
    case 1u: return 3u;
    case 2u: return 4u;
    case 3u: return 1u;
    default: return 2u;
  }
}

inline UINT D3DFVFTexcoordBytes(DWORD fvf, UINT coord_index) {
  return D3DFVFTexcoordComponentCount(fvf, coord_index) * static_cast<UINT>(sizeof(float));
}

inline UINT D3DFVFTexcoordOffsetBytes(DWORD fvf, UINT coord_index) {
  UINT offset = 0;
  const UINT tex_count = D3DFVFTexcoordCount(fvf);
  const UINT safe_index = std::min(coord_index, tex_count);
  for (UINT i = 0; i < safe_index; ++i) offset += D3DFVFTexcoordBytes(fvf, i);
  return offset;
}

inline UINT D3DFVFTexcoordPayloadBytes(DWORD fvf) {
  return D3DFVFTexcoordOffsetBytes(fvf, D3DFVFTexcoordCount(fvf));
}

inline UINT D3DFVFEffectiveStrideFromTexcoordBase(
    DWORD fvf,
    UINT fallback_stride,
    UINT texcoord_base_offset) {
  if (fallback_stride > 0) return fallback_stride;
  return texcoord_base_offset + D3DFVFTexcoordPayloadBytes(fvf);
}

inline UINT D3DFVFEffectiveStride(DWORD fvf, UINT fallback_stride) {
  return D3DFVFEffectiveStrideFromTexcoordBase(
      fvf,
      fallback_stride,
      D3DFVFTexcoordBaseOffsetBytes(fvf));
}

inline bool D3DFVFTexcoordSpanFitsStride(DWORD fvf, UINT texcoord_base_offset, UINT stride) {
  if (texcoord_base_offset > stride) return false;
  const UINT payload_bytes = D3DFVFTexcoordPayloadBytes(fvf);
  return payload_bytes <= stride - texcoord_base_offset;
}

struct D3DFVFTexcoordPair {
  float u = 0.0f;
  float v = 0.0f;
  bool valid = false;
};

inline D3DFVFTexcoordPair D3DFVFReadTexcoordPair(
    const void* vertex,
    UINT stride,
    DWORD fvf,
    UINT texcoord_base_offset,
    UINT coord_index) {
  D3DFVFTexcoordPair out{};
  if (!vertex || coord_index >= D3DFVFTexcoordCount(fvf)) return out;

  const UINT coord_offset = D3DFVFTexcoordOffsetBytes(fvf, coord_index);
  const UINT coord_bytes = D3DFVFTexcoordBytes(fvf, coord_index);
  if (texcoord_base_offset > stride || coord_offset > stride - texcoord_base_offset) return out;
  const UINT byte_offset = texcoord_base_offset + coord_offset;
  if (coord_bytes < sizeof(float) || coord_bytes > stride - byte_offset) return out;

  const BYTE* field = static_cast<const BYTE*>(vertex) + byte_offset;
  std::memcpy(&out.u, field, sizeof(float));
  if (D3DFVFTexcoordComponentCount(fvf, coord_index) > 1u) {
    std::memcpy(&out.v, field + sizeof(float), sizeof(float));
  }
  out.valid = true;
  return out;
}

inline D3DFVFTexcoordPair D3DFVFReadTexcoordPairOrFallback(
    const void* vertex,
    UINT stride,
    DWORD fvf,
    UINT texcoord_base_offset,
    UINT coord_index,
    const D3DFVFTexcoordPair& fallback) {
  D3DFVFTexcoordPair out = D3DFVFReadTexcoordPair(vertex, stride, fvf, texcoord_base_offset, coord_index);
  return out.valid ? out : fallback;
}

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

using HRESULT = long;
#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif
#ifndef S_FALSE
#define S_FALSE ((HRESULT)1L)
#endif
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif
#ifndef E_POINTER
#define E_POINTER ((HRESULT)0x80004003L)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif
#ifndef E_UNEXPECTED
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#endif
#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

extern "C" FILE* wyd_fopen_case_insensitive(const char* filename, const char* mode);

inline int fopen_s(FILE** pFile, const char* filename, const char* mode) {
  if (!pFile) return EINVAL;
  *pFile = wyd_fopen_case_insensitive(filename, mode);
  return *pFile ? 0 : errno;
}

inline int freopen_s(FILE** pFile, const char* filename, const char* mode, FILE* stream) {
  if (!pFile) return EINVAL;
  *pFile = std::freopen(filename, mode, stream);
  return *pFile ? 0 : errno;
}

inline int sscanf_s(const char* buffer, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int ret = std::vsscanf(buffer, format, args);
  va_end(args);
  return ret;
}

inline int strncpy_s(char* dest, const char* src, size_t count) {
  if (!dest || !src) return EINVAL;
  if (count == 0) return 0;
  std::strncpy(dest, src, count);
  dest[count - 1] = '\0';
  return 0;
}

inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count) {
  if (!dest || !src || destsz == 0) return EINVAL;
  size_t n = std::min(destsz - 1, count);
  std::strncpy(dest, src, n);
  dest[n] = '\0';
  return 0;
}

template <size_t N>
inline int sprintf_s(char (&buffer)[N], const char* format, ...) {
  va_list args;
  va_start(args, format);
  int ret = std::vsnprintf(buffer, N, format, args);
  va_end(args);
  return ret;
}

template <size_t N>
inline int sprintf_s(char (&buffer)[N], size_t sizeOfBuffer, const char* format, ...) {
  size_t cap = std::min<size_t>(N, sizeOfBuffer);
  va_list args;
  va_start(args, format);
  int ret = std::vsnprintf(buffer, cap, format, args);
  va_end(args);
  return ret;
}

inline int sprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, ...) {
  if (!buffer || sizeOfBuffer == 0) return EINVAL;
  va_list args;
  va_start(args, format);
  int ret = std::vsnprintf(buffer, sizeOfBuffer, format, args);
  va_end(args);
  return ret;
}

#ifndef _malloca
#define _malloca _alloca
#endif

inline char* _itoa(int value, char* buffer, int radix) {
  if (!buffer) return nullptr;
  if (radix == 16) std::snprintf(buffer, 34, "%x", value);
  else std::snprintf(buffer, 34, "%d", value);
  return buffer;
}

inline long long _atoi64(const char* s) {
  return s ? std::strtoll(s, nullptr, 10) : 0;
}
