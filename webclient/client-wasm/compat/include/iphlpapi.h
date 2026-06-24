#pragma once
#include "windows.h"

struct IP_ADAPTER_INFO {
  struct IP_ADAPTER_INFO* Next;
  DWORD ComboIndex;
  char AdapterName[260];
  char Description[132];
  UINT AddressLength;
  BYTE Address[8];
  DWORD Index;
  UINT Type;
  UINT DhcpEnabled;
  void* CurrentIpAddress;
  char IpAddressList[16];
};
using PIP_ADAPTER_INFO = IP_ADAPTER_INFO*;

#ifdef __cplusplus
extern "C" {
#endif

DWORD GetAdaptersInfo(PIP_ADAPTER_INFO pAdapterInfo, void* pOutBufLen);

#ifdef __cplusplus
}
#endif
