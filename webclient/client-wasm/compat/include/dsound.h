#pragma once
#include "windows.h"
#include "d3d9.h"
#include "mmreg.h"
#include "unknwn.h"

struct IDirectSound8;
struct IDirectSoundBuffer;
struct IDirectSound3DBuffer;
struct IDirectSound3DListener;

using LPDIRECTSOUND8 = IDirectSound8*;
using LPDIRECTSOUNDBUFFER = IDirectSoundBuffer*;
using LPDIRECTSOUND3DBUFFER = IDirectSound3DBuffer*;
using LPDS3DBUFFER = IDirectSound3DBuffer*;
using LPDIRECTSOUND3DLISTENER = IDirectSound3DListener*;

#ifndef DS_OK
#define DS_OK 0
#endif
#ifndef DSSCL_PRIORITY
#define DSSCL_PRIORITY 0
#endif
#ifndef DSBCAPS_CTRL3D
#define DSBCAPS_CTRL3D 0x00000010
#endif
#ifndef DSBCAPS_PRIMARYBUFFER
#define DSBCAPS_PRIMARYBUFFER 0x00000001
#endif
#ifndef DSBCAPS_CTRLVOLUME
#define DSBCAPS_CTRLVOLUME 0x00000080
#endif
#ifndef DSBCAPS_STATIC
#define DSBCAPS_STATIC 0x00000002
#endif
#ifndef DSBCAPS_LOCSOFTWARE
#define DSBCAPS_LOCSOFTWARE 0x00000008
#endif
#ifndef DS3D_IMMEDIATE
#define DS3D_IMMEDIATE 0x00000000
#endif
#ifndef DSBPLAY_LOOPING
#define DSBPLAY_LOOPING 0x00000001
#endif
#ifndef DSERR_BUFFERTOOSMALL
#define DSERR_BUFFERTOOSMALL ((HRESULT)0x88780096L)
#endif
#ifndef DSERR_BUFFERLOST
#define DSERR_BUFFERLOST ((HRESULT)0x88780078L)
#endif
#ifndef DS_NO_VIRTUALIZATION
#define DS_NO_VIRTUALIZATION ((HRESULT)0x0878000AL)
#endif

struct DSBUFFERDESC {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwReserved;
  LPWAVEFORMATEX lpwfxFormat;
  GUID guid3DAlgorithm;
};
using _DSBUFFERDESC = DSBUFFERDESC;

struct DS3DBUFFER {
  DWORD dwSize;
  D3DVECTOR vPosition;
  D3DVECTOR vVelocity;
  DWORD dwInsideConeAngle;
  DWORD dwOutsideConeAngle;
  D3DVECTOR vConeOrientation;
  LONG lConeOutsideVolume;
  float flMinDistance;
  float flMaxDistance;
  DWORD dwMode;
};

struct DS3DLISTENER {
  DWORD dwSize;
  D3DVECTOR vPosition;
  D3DVECTOR vVelocity;
  D3DVECTOR vOrientFront;
  D3DVECTOR vOrientTop;
  float flDistanceFactor;
  float flRolloffFactor;
  float flDopplerFactor;
};

struct IDirectSoundBuffer : public IUnknown {
  template <typename... Args>
  HRESULT GetStatus(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT Restore(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT SetFormat(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT SetVolume(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT SetCurrentPosition(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT GetCurrentPosition(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT Lock(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT Unlock(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT Play(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT Stop(Args...) { return E_NOTIMPL; }
};

struct IDirectSound3DBuffer : public IUnknown {
  template <typename... Args>
  HRESULT GetAllParameters(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT SetAllParameters(Args...) { return E_NOTIMPL; }
};

struct IDirectSound3DListener : public IUnknown {
  template <typename... Args>
  HRESULT GetAllParameters(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT SetAllParameters(Args...) { return E_NOTIMPL; }
};

struct IDirectSound8 : public IUnknown {
  template <typename... Args>
  HRESULT SetCooperativeLevel(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT CreateSoundBuffer(Args...) { return E_NOTIMPL; }
  template <typename... Args>
  HRESULT DuplicateSoundBuffer(Args...) { return E_NOTIMPL; }
};

inline constexpr GUID IID_IDirectSound3DListener = {0x279afa83, 0x4981, 0x11ce, {0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60}};
inline constexpr GUID IID_IDirectSound3DBuffer = {0x279afa86, 0x4981, 0x11ce, {0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60}};

#ifdef __cplusplus
extern "C" {
#endif

HRESULT DirectSoundCreate8(const GUID* pcGuidDevice, LPDIRECTSOUND8* ppDS8, IUnknown* pUnkOuter);

#ifdef __cplusplus
}
#endif
