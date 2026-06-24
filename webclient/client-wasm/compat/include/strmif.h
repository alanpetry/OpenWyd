#pragma once
#include "objbase.h"

using OAHWND = LONG_PTR;

using REFERENCE_TIME = long long;
using OAEVENT = LONG_PTR;

enum FILTER_STATE {
  State_Stopped = 0,
  State_Paused = 1,
  State_Running = 2,
};

struct IBaseFilter : public IUnknown {
  virtual HRESULT FindPin(LPCWSTR Id, struct IPin** ppPin) = 0;
};

struct IPin : public IUnknown {
};

struct IEnumFilters : public IUnknown {
  virtual HRESULT Next(ULONG cFilters, IBaseFilter** ppFilter, ULONG* pcFetched) = 0;
  virtual HRESULT Skip(ULONG cFilters) = 0;
  virtual HRESULT Reset() = 0;
};

struct IGraphBuilder : public IUnknown {
  virtual HRESULT AddFilter(IBaseFilter* pFilter, LPCWSTR pName) = 0;
  virtual HRESULT RemoveFilter(IBaseFilter* pFilter) = 0;
  virtual HRESULT EnumFilters(IEnumFilters** ppEnum) = 0;
  virtual HRESULT FindFilterByName(LPCWSTR pName, IBaseFilter** ppFilter) = 0;
  virtual HRESULT ConnectDirect(void* ppinOut, void* ppinIn, const void* pmt) = 0;
  virtual HRESULT Reconnect(void* ppin) = 0;
  virtual HRESULT Disconnect(void* ppin) = 0;
  virtual HRESULT SetDefaultSyncSource() = 0;
  virtual HRESULT Connect(void* ppinOut, void* ppinIn) = 0;
  virtual HRESULT Render(void* ppinOut) = 0;
  virtual HRESULT RenderFile(LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList) = 0;
  virtual HRESULT AddSourceFilter(LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrFilterName, IBaseFilter** ppFilter) = 0;
};

struct IMediaControl : public IUnknown {
  virtual HRESULT Run() = 0;
  virtual HRESULT Pause() = 0;
  virtual HRESULT Stop() = 0;
};

struct IMediaSeeking : public IUnknown {
  virtual HRESULT GetCapabilities(DWORD* pCapabilities) = 0;
  virtual HRESULT CheckCapabilities(DWORD* pCapabilities) = 0;
  virtual HRESULT IsFormatSupported(const GUID* pFormat) = 0;
  virtual HRESULT QueryPreferredFormat(GUID* pFormat) = 0;
  virtual HRESULT GetTimeFormat(GUID* pFormat) = 0;
  virtual HRESULT IsUsingTimeFormat(const GUID* pFormat) = 0;
  virtual HRESULT SetTimeFormat(const GUID* pFormat) = 0;
  virtual HRESULT GetDuration(LONGLONG* pDuration) = 0;
  virtual HRESULT GetStopPosition(LONGLONG* pStop) = 0;
  virtual HRESULT GetCurrentPosition(LONGLONG* pCurrent) = 0;
  virtual HRESULT ConvertTimeFormat(LONGLONG* pTarget, const GUID* pTargetFormat, LONGLONG Source, const GUID* pSourceFormat) = 0;
  virtual HRESULT SetPositions(LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags) = 0;
};

struct IBasicAudio : public IUnknown {
  virtual HRESULT put_Volume(long lVolume) = 0;
  virtual HRESULT get_Volume(long* plVolume) = 0;
  virtual HRESULT put_Balance(long lBalance) = 0;
  virtual HRESULT get_Balance(long* plBalance) = 0;
};

struct IMediaEventEx : public IUnknown {
  virtual HRESULT GetEventHandle(OAEVENT* hEvent) = 0;
  virtual HRESULT GetEvent(long* lEventCode, long* lParam1, long* lParam2, long msTimeout) = 0;
  virtual HRESULT WaitForCompletion(long msTimeout, long* pEvCode) = 0;
  virtual HRESULT CancelDefaultHandling(long lEvCode) = 0;
  virtual HRESULT RestoreDefaultHandling(long lEvCode) = 0;
  virtual HRESULT FreeEventParams(long lEvCode, long lParam1, long lParam2) = 0;
  virtual HRESULT SetNotifyWindow(OAHWND hwnd, long lMsg, LONG_PTR lInstanceData) = 0;
  virtual HRESULT SetNotifyFlags(long lNoNotifyFlags) = 0;
  virtual HRESULT GetNotifyFlags(long* lplNoNotifyFlags) = 0;
};

inline constexpr GUID CLSID_FilterGraph = {0xe436ebb3, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
inline constexpr GUID IID_IGraphBuilder = {0x56a868a9, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
inline constexpr GUID IID_IMediaControl = {0x56a868b1, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
inline constexpr GUID IID_IMediaSeeking = {0x36b73880, 0xc2c8, 0x11cf, {0x8b, 0x46, 0x00, 0x80, 0x5f, 0x6c, 0xef, 0x60}};
inline constexpr GUID IID_IBasicAudio = {0x56a868b3, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
inline constexpr GUID IID_IMediaEventEx = {0x56a868c0, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
