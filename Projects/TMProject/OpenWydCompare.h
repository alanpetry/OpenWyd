#pragma once

#if defined(OPENWYD_COMPARE) && defined(_DEBUG) && !defined(__EMSCRIPTEN__)

#include <Windows.h>

struct IDirect3DDevice9;

// The native comparison bridge is armed only when OPENWYD_COMPARE_PIPE is set.
// All functions are called from the client main thread, except for the clock
// wrappers, which are safe to call from any client thread.
bool OpenWydCompareArmRandomFromEnvironment();
bool OpenWydCompareInitialize(HWND hWnd, unsigned int width, unsigned int height);
void OpenWydComparePoll();
bool OpenWydCompareIsEnabled();
bool OpenWydCompareTryBeginFrame();
bool OpenWydCompareTakePausedControlMessage(MSG* message);
bool OpenWydCompareShouldDispatchMessage(const MSG* message);
bool OpenWydCompareTakeInjectedMouseMessage(
	UINT message,
	WPARAM* wParam,
	int x,
	int y);
bool OpenWydCompareConsumeMouseState(
	LONG* deltaX,
	LONG* deltaY,
	LONG* wheel,
	BYTE* buttons,
	unsigned int buttonCapacity);
bool OpenWydCompareTakeInjectedKeyMessage(
	bool down,
	WPARAM wParam,
	LPARAM* lParam);
bool OpenWydCompareInjectedKeyIsDown(unsigned int virtualKey);
void OpenWydCompareCapture3DState(IDirect3DDevice9* device);
void OpenWydCompareOnBeforePresent(IDirect3DDevice9* device);
void OpenWydCompareOnAfterPresent(HRESULT presentResult);
void OpenWydCompareOnFrameTickComplete();
void OpenWydCompareResolveServerEndpoint(
	const char* originalHost,
	int originalPort,
	char* resolvedHost,
	unsigned int resolvedHostCapacity,
	int* resolvedPort);
void OpenWydCompareShutdown();

DWORD WINAPI OpenWydCompareTimeGetTime();
DWORD WINAPI OpenWydCompareGetTickCount();

#endif
