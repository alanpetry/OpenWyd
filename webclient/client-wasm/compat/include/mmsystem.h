#pragma once
#include "windows.h"

using MMRESULT = UINT;
using FOURCC = DWORD;
using HMMIO = HANDLE;

struct MMIOINFO {
  DWORD dwFlags;
  FOURCC fccIOProc;
  LPARAM pIOProc;
  UINT wErrorRet;
  HTASK htask;
  LONG cchBuffer;
  HPSTR pchBuffer;
  HPSTR pchNext;
  HPSTR pchEndRead;
  HPSTR pchEndWrite;
  LONG lBufOffset;
  LONG lDiskOffset;
  DWORD adwInfo[3];
  DWORD dwReserved1;
  DWORD dwReserved2;
  HMMIO hmmio;
};

struct MMCKINFO {
  FOURCC ckid;
  DWORD cksize;
  FOURCC fccType;
  DWORD dwDataOffset;
  DWORD dwFlags;
};
using _MMCKINFO = MMCKINFO;
using _MMIOINFO = MMIOINFO;

#ifndef MMIO_ALLOCBUF
#define MMIO_ALLOCBUF 0x00010000
#endif
#ifndef MMIO_DIRTY
#define MMIO_DIRTY 0x10000000
#endif
#ifndef MMIO_READ
#define MMIO_READ 0x00000000
#endif
#ifndef MMIO_WRITE
#define MMIO_WRITE 0x00000001
#endif

#ifdef __cplusplus
extern "C" {
#endif

DWORD timeGetTime(void);
HMMIO mmioOpenA(LPSTR pszFileName, MMIOINFO* pmmioinfo, DWORD fdwOpen);
MMRESULT mmioClose(HMMIO hmmio, UINT wFlags);
MMRESULT mmioDescend(HMMIO hmmio, MMCKINFO* pmmcki, const MMCKINFO* pmmckiParent, UINT wFlags);
MMRESULT mmioAscend(HMMIO hmmio, MMCKINFO* pmmcki, UINT wFlags);
LONG mmioRead(HMMIO hmmio, HPSTR pch, LONG cch);
LONG mmioWrite(HMMIO hmmio, const char* pch, LONG cch);
MMRESULT mmioCreateChunk(HMMIO hmmio, MMCKINFO* pmmcki, UINT wFlags);
MMRESULT mmioGetInfo(HMMIO hmmio, MMIOINFO* pmmioinfo, UINT wFlags);
MMRESULT mmioSetInfo(HMMIO hmmio, MMIOINFO* pmmioinfo, UINT wFlags);
MMRESULT mmioAdvance(HMMIO hmmio, MMIOINFO* pmmioinfo, UINT wFlags);
LONG mmioSeek(HMMIO hmmio, LONG lOffset, int iOrigin);

#ifdef __cplusplus
}
#endif
