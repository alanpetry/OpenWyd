#pragma once
#include "unknwn.h"

struct IDirectInput8A;
struct IDirectInputDevice8A;

using IDirectInput8 = IDirectInput8A;
using IDirectInputDevice8 = IDirectInputDevice8A;

using LPDIRECTINPUT8 = IDirectInput8A*;
using LPDIRECTINPUT8A = IDirectInput8A*;
using LPDIRECTINPUTDEVICE8 = IDirectInputDevice8A*;
using LPDIRECTINPUTDEVICE8A = IDirectInputDevice8A*;

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#ifndef DISCL_FOREGROUND
#define DISCL_FOREGROUND 0x00000001
#endif
#ifndef DISCL_NONEXCLUSIVE
#define DISCL_NONEXCLUSIVE 0x00000002
#endif
#ifndef DISCL_EXCLUSIVE
#define DISCL_EXCLUSIVE 0x00000001
#endif
#ifndef DISCL_BACKGROUND
#define DISCL_BACKGROUND 0x00000000
#endif

struct DIDEVCAPS {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwDevType;
  DWORD dwAxes;
  DWORD dwButtons;
  DWORD dwPOVs;
  DWORD dwFFSamplePeriod;
  DWORD dwFFMinTimeResolution;
  DWORD dwFirmwareRevision;
  DWORD dwHardwareRevision;
  DWORD dwFFDriverVersion;
};

struct DIDATAFORMAT {
  DWORD dwSize;
  DWORD dwObjSize;
  DWORD dwFlags;
  DWORD dwDataSize;
  DWORD dwNumObjs;
  const void* rgodf;
};

struct DIMOUSESTATE2 {
  LONG lX;
  LONG lY;
  LONG lZ;
  BYTE rgbButtons[8];
};

struct IDirectInputDevice8A : public IUnknown {
  virtual HRESULT GetCapabilities(DIDEVCAPS* lpDIDevCaps) = 0;
  virtual HRESULT Acquire() = 0;
  virtual HRESULT Unacquire() = 0;
  virtual HRESULT GetDeviceState(DWORD cbData, LPVOID lpvData) = 0;
  virtual HRESULT SetDataFormat(const DIDATAFORMAT* lpdf) = 0;
  virtual HRESULT SetCooperativeLevel(HWND hwnd, DWORD dwFlags) = 0;
};

struct IDirectInput8A : public IUnknown {
  virtual HRESULT CreateDevice(REFGUID rguid, IDirectInputDevice8A** lplpDirectInputDevice, IUnknown* pUnkOuter) = 0;
};

inline constexpr GUID IID_IDirectInput8A = {0xbf798031, 0x483a, 0x4da2, {0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0x00}};
inline constexpr GUID GUID_SysMouse = {0x6f1d2b60, 0xd5a0, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr DIDATAFORMAT c_dfDIMouse2 = {sizeof(DIDATAFORMAT), 0, 0, sizeof(DIMOUSESTATE2), 0, nullptr};

#ifdef __cplusplus
extern "C" {
#endif

HRESULT DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, void** ppvOut, IUnknown* punkOuter);

#ifdef __cplusplus
}
#endif
