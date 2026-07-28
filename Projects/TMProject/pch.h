#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"

#include <Windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include <time.h>

#define DIRECTINPUT_VERSION 0x0800

#pragma warning(push, 0)        
#include <d3d9.h>
#include <d3dx9.h>
#include <Dshow.h>
#pragma warning(pop)

#include <iostream>
#include <fileapi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iphlpapi.h>
#include <chrono>
using namespace std::chrono_literals;

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "Strmiids.lib")

#if defined(OPENWYD_COMPARE) && defined(_DEBUG) && !defined(__EMSCRIPTEN__)
#include "OpenWydCompare.h"
#define timeGetTime OpenWydCompareTimeGetTime
#define GetTickCount OpenWydCompareGetTickCount
#endif

#if defined(__EMSCRIPTEN__) || \
	(defined(OPENWYD_COMPARE) && defined(_DEBUG))
#include "OpenWydCompareRandom.h"
#define rand OpenWydCompareRandomRand
#define srand OpenWydCompareRandomSrand
#endif

#endif //PCH_H
