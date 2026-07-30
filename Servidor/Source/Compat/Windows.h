#pragma once

#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#define WINAPI
#define APIENTRY
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR

using BYTE = std::uint8_t;
using UCHAR = std::uint8_t;
using CHAR = char;
using WORD = std::uint16_t;
using USHORT = std::uint16_t;
using SHORT = std::int16_t;
using INT16 = std::int16_t;
using UINT16 = std::uint16_t;
using INT32 = std::int32_t;
using UINT32 = std::uint32_t;
using DWORD = std::uint32_t;
using ULONG = std::uint32_t;
using UINT = std::uint32_t;
using BOOL = int;
using INT = int;
using LONG = std::int32_t;
using LONGLONG = std::int64_t;
using ULONGLONG = std::uint64_t;
using __int64 = std::int64_t;
using FLOAT = float;
#define __int8 char
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using HANDLE = void*;
using HINSTANCE = void*;
using HWND = void*;
using HMENU = void*;
using HICON = void*;
using HCURSOR = void*;
using HBRUSH = void*;
using HDC = void*;
struct HFONT__ {};
using HFONT = HFONT__*;
using HGDIOBJ = void*;
using ATOM = std::uint16_t;

constexpr BOOL FALSE = 0;
constexpr BOOL TRUE = 1;
constexpr DWORD INFINITE = 0xffffffffu;
constexpr DWORD MAX_PATH = 260;

using WNDPROC = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

struct WNDCLASS {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
};

struct POINT {
    LONG x;
    LONG y;
};

struct MSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
};

struct PAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    long reserved[8];
};

struct SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

struct WIN32_FIND_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    char cFileName[MAX_PATH];
};

using LPTHREAD_START_ROUTINE = DWORD(WINAPI*)(LPVOID);

#define LOWORD(value) static_cast<WORD>(static_cast<std::uintptr_t>(value) & 0xffffu)
#define HIWORD(value) static_cast<WORD>((static_cast<std::uintptr_t>(value) >> 16u) & 0xffffu)
#define HIBYTE(value) static_cast<BYTE>((static_cast<std::uintptr_t>(value) >> 8u) & 0xffu)
#define LOBYTE(value) static_cast<BYTE>(static_cast<std::uintptr_t>(value) & 0xffu)
#define MAKELPARAM(low, high) static_cast<LPARAM>((static_cast<DWORD>(low) & 0xffffu) | (static_cast<DWORD>(high) << 16u))
#define MAKEWPARAM(low, high) static_cast<WPARAM>((static_cast<DWORD>(low) & 0xffffu) | (static_cast<DWORD>(high) << 16u))
#define UNREFERENCED_PARAMETER(value) (void)(value)
#define SendMessage SendMessageA

constexpr UINT WM_NULL = 0x0000;
constexpr UINT WM_CREATE = 0x0001;
constexpr UINT WM_DESTROY = 0x0002;
constexpr UINT WM_CLOSE = 0x0010;
constexpr UINT WM_QUIT = 0x0012;
constexpr UINT WM_COMMAND = 0x0111;
constexpr UINT WM_TIMER = 0x0113;
constexpr UINT WM_PAINT = 0x000f;
constexpr UINT WM_USER = 0x0400;

constexpr UINT CS_VREDRAW = 0x0001;
constexpr UINT CS_HREDRAW = 0x0002;
constexpr UINT CS_DBLCLKS = 0x0008;
constexpr DWORD WS_OVERLAPPED = 0x00000000;
constexpr DWORD WS_SYSMENU = 0x00080000;
constexpr DWORD WS_MINIMIZEBOX = 0x00020000;
constexpr DWORD WS_MAXIMIZEBOX = 0x00010000;
constexpr DWORD WS_OVERLAPPEDWINDOW = 0x00cf0000;
constexpr DWORD WS_CLIPCHILDREN = 0x02000000;
constexpr int CW_USEDEFAULT = static_cast<int>(0x80000000u);
#define IDC_ARROW reinterpret_cast<LPCSTR>(32512)
constexpr int WHITE_BRUSH = 0;

constexpr UINT MB_OK = 0;
constexpr UINT MB_SYSTEMMODAL = 0x00001000;
constexpr UINT MB_ICONQUESTION = 0x00000020;
constexpr UINT MB_YESNO = 0x00000004;
constexpr int IDNO = 7;
constexpr UINT MF_STRING = 0;
constexpr UINT MF_POPUP = 0x10;
constexpr DWORD FILE_ATTRIBUTE_DIRECTORY = 0x10;
constexpr int FW_LIGHT = 300;
constexpr DWORD DEFAULT_CHARSET = 1;
constexpr DWORD OUT_DEVICE_PRECIS = 5;
constexpr DWORD CLIP_DEFAULT_PRECIS = 0;
constexpr DWORD DEFAULT_QUALITY = 0;
constexpr DWORD DEFAULT_PITCH = 0;

ATOM RegisterClass(const WNDCLASS* window_class);
HWND CreateWindow(LPCSTR class_name, LPCSTR title, DWORD style, int x, int y,
                  int width, int height, HWND parent, HMENU menu,
                  HINSTANCE instance, LPVOID parameter);
BOOL ShowWindow(HWND window, int command);
BOOL UpdateWindow(HWND window);
BOOL DestroyWindow(HWND window);
LRESULT DefWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL SetWindowText(HWND window, LPCSTR text);
BOOL SetWindowTextA(HWND window, LPCSTR text);
HICON LoadIcon(HINSTANCE instance, LPCSTR name);
HCURSOR LoadCursor(HINSTANCE instance, LPCSTR name);
HGDIOBJ GetStockObject(int object);
HMENU CreateMenu();
HMENU CreatePopupMenu();
BOOL AppendMenu(HMENU menu, UINT flags, std::uintptr_t item, LPCSTR text);
BOOL SetMenu(HWND window, HMENU menu);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
DWORD SetTextColor(HDC dc, DWORD color);
BOOL TextOut(HDC dc, int x, int y, LPCSTR text, int length);
BOOL TextOutA(HDC dc, int x, int y, LPCSTR text, int length);
HDC BeginPaint(HWND window, PAINTSTRUCT* paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT* paint);
HFONT CreateFont(int height, int width, int escapement, int orientation,
                 int weight, DWORD italic, DWORD underline, DWORD strikeout,
                 DWORD charset, DWORD output_precision, DWORD clip_precision,
                 DWORD quality, DWORD pitch_and_family, LPCSTR face);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);

int MessageBox(HWND window, LPCSTR text, LPCSTR caption, UINT type);
int MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type);
void ExitProcess(UINT exit_code);
DWORD GetLastError();
DWORD GetTickCount();
DWORD timeGetTime();
void Sleep(DWORD milliseconds);
void GetLocalTime(SYSTEMTIME* value);
DWORD GetModuleFileName(HINSTANCE module, LPSTR filename, DWORD size);
BOOL SetCurrentDirectory(LPCSTR path);
BOOL DeleteFileA(LPCSTR path);
BOOL MoveFile(LPCSTR source, LPCSTR destination);
HANDLE FindFirstFile(LPCSTR pattern, WIN32_FIND_DATA* data);
BOOL FindNextFile(HANDLE search, WIN32_FIND_DATA* data);
BOOL FindClose(HANDLE search);
BOOL FileTimeToSystemTime(const FILETIME* file_time, SYSTEMTIME* system_time);

HANDLE CreateThread(LPVOID attributes, std::size_t stack_size,
                    LPTHREAD_START_ROUTINE start, LPVOID parameter,
                    DWORD flags, DWORD* thread_id);
BOOL CloseHandle(HANDLE handle);

std::uintptr_t SetTimer(HWND window, std::uintptr_t id, UINT milliseconds, LPVOID callback);
BOOL KillTimer(HWND window, std::uintptr_t id);
BOOL GetMessage(MSG* message, HWND window, UINT minimum, UINT maximum);
BOOL TranslateMessage(const MSG* message);
LRESULT DispatchMessage(const MSG* message);
BOOL PostMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void PostQuitMessage(int exit_code);

void openwyd_set_window_proc(WNDPROC proc);

#include "winsock2.h"

#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1))
#define DeleteFile DeleteFileA

inline int fopen_s(FILE** output, const char* path, const char* mode) {
    if (!output)
        return 22;
    *output = std::fopen(path, mode);
    return *output ? 0 : errno;
}

inline char* CharNext(const char* text) {
    return const_cast<char*>(text && *text ? text + 1 : text);
}

inline char* _itoa(int value, char* output, int radix) {
    if (!output)
        return output;
    if (radix == 16)
        std::sprintf(output, "%x", value);
    else if (radix == 8)
        std::sprintf(output, "%o", value);
    else
        std::sprintf(output, "%d", value);
    return output;
}
