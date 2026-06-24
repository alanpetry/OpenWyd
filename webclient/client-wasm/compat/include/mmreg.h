#pragma once
#include "windows.h"

using WORD = std::uint16_t;

struct WAVEFORMAT {
  WORD wFormatTag;
  WORD nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD nBlockAlign;
};

struct WAVEFORMATEX {
  WORD wFormatTag;
  WORD nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD nBlockAlign;
  WORD wBitsPerSample;
  WORD cbSize;
};

struct PCMWAVEFORMAT {
  WAVEFORMAT wf;
  WORD wBitsPerSample;
};

using LPWAVEFORMAT = WAVEFORMAT*;
using LPWAVEFORMATEX = WAVEFORMATEX*;
using tWAVEFORMATEX = WAVEFORMATEX;
using pcmwaveformat_tag = PCMWAVEFORMAT;
