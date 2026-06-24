#pragma once
#include "winsock.h"

struct WSAData {
  WORD wVersion;
  WORD wHighVersion;
  char szDescription[257];
  char szSystemStatus[129];
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
};

#ifdef __cplusplus
extern "C" {
#endif

int WSAStartup(WORD wVersionRequested, WSAData* lpWSAData);
int WSACleanup(void);
int WSAGetLastError(void);
int WSAAsyncSelect(SOCKET s, HWND hWnd, unsigned int wMsg, long lEvent);

#ifdef __cplusplus
}
#endif
