#pragma once
#include "tm_emscripten_prelude.h"
#include "basetsd.h"
#include "guiddef.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

using VOID = void;
using PVOID = void*;
using LPVOID = void*;
using LPCVOID = const void*;

using CHAR = char;
using SHORT = short;
using USHORT = unsigned short;
using INT8 = std::int8_t;
using UINT8 = std::uint8_t;
using INT16 = std::int16_t;
using UINT16 = std::uint16_t;
using INT32 = std::int32_t;
using UINT32 = std::uint32_t;
using INT64 = std::int64_t;
using UINT64 = std::uint64_t;

using BOOL = int;
using BOOLEAN = BYTE;
using DWORD = std::uint32_t;
using DWORDLONG = std::uint64_t;
using WORD = std::uint16_t;
using BYTE = std::uint8_t;
using LONG = long;
using ULONG = unsigned long;
using UINT = unsigned int;
using INT = int;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using ATOM = WORD;
using LCID = DWORD;
using LANGID = WORD;
using COLORREF = DWORD;

using HANDLE = void*;
using HINSTANCE = HANDLE;
using HMODULE = HANDLE;
using HICON = HANDLE;
using HCURSOR = HANDLE;
using HBRUSH = HANDLE;
using HDC = HANDLE;
using HWND = HANDLE;
using HMENU = HANDLE;
using HKEY = HANDLE;
using HFONT = HANDLE;
using HGDIOBJ = HANDLE;
using HBITMAP = HANDLE;
using HPALETTE = HANDLE;
using HMONITOR = HANDLE;
using HGLOBAL = HANDLE;
using HKL = HANDLE;
using HIMC = HANDLE;
using HLOCAL = HANDLE;
using HTASK = HANDLE;
using HACCEL = HANDLE;
struct HRSRC__;
using HRSRC = HRSRC__*;
using FARPROC = void (*)();

using LONGLONG = long long;
using ULONGLONG = unsigned long long;
using DWORD32 = std::uint32_t;
using DWORD64 = std::uint64_t;
using HPSTR = char*;

using LPBYTE = BYTE*;
using LPCBYTE = const BYTE*;
using LPWORD = WORD*;
using LPDWORD = DWORD*;
using LPINT = int*;
using LPLONG = LONG*;
using LPBOOL = BOOL*;

using LPCSTR = const char*;
using LPSTR = char*;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
using TCHAR = char;
using LPTSTR = char*;
using LPCTSTR = const char*;
using REGSAM = DWORD;
using PHKEY = HKEY*;

using HRESULT = long;

inline char* _strupr(char* s) {
  if (!s) return s;
  for (char* p = s; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - ('a' - 'A'));
  }
  return s;
}

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef NULL
#define NULL 0
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

#ifndef WINAPI_FAMILY_DESKTOP_APP
#define WINAPI_FAMILY_DESKTOP_APP 1
#endif

#ifndef MAKEWORD
#define MAKEWORD(a, b) ((WORD)(((BYTE)((a) & 0xff)) | ((WORD)((BYTE)((b) & 0xff))) << 8))
#endif
#ifndef MAKEINTRESOURCEA
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#endif
#ifndef MAKEINTRESOURCE
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#endif
#ifndef MAKELONG
#define MAKELONG(a, b) ((LONG)(((WORD)((a) & 0xffff)) | ((DWORD)((WORD)((b) & 0xffff))) << 16))
#endif
#ifndef LOWORD
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#endif
#ifndef LOBYTE
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#endif
#ifndef HIBYTE
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#endif

#ifndef MB_OK
#define MB_OK 0x00000000L
#endif
#ifndef MB_ICONERROR
#define MB_ICONERROR 0x00000010L
#endif
#ifndef MB_ICONWARNING
#define MB_ICONWARNING 0x00000030L
#endif
#ifndef MB_SYSTEMMODAL
#define MB_SYSTEMMODAL 0x00001000L
#endif

#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000L
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE 0x40000000L
#endif
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ 0x00000001L
#endif
#ifndef FILE_SHARE_WRITE
#define FILE_SHARE_WRITE 0x00000002L
#endif
#ifndef OPEN_EXISTING
#define OPEN_EXISTING 3
#endif
#ifndef CREATE_ALWAYS
#define CREATE_ALWAYS 2
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#ifndef FILE_BEGIN
#define FILE_BEGIN 0
#endif
#ifndef FILE_CURRENT
#define FILE_CURRENT 1
#endif
#ifndef FILE_END
#define FILE_END 2
#endif

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0x00000000L
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 0x00000102L
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif

#ifndef WM_USER
#define WM_USER 0x0400
#endif
#ifndef WM_QUIT
#define WM_QUIT 0x0012
#endif
#ifndef WM_DESTROY
#define WM_DESTROY 0x0002
#endif
#ifndef WM_CREATE
#define WM_CREATE 0x0001
#endif
#ifndef WM_CLOSE
#define WM_CLOSE 0x0010
#endif
#ifndef WM_MOVE
#define WM_MOVE 0x0003
#endif
#ifndef WM_ACTIVATE
#define WM_ACTIVATE 0x0006
#endif
#ifndef WM_PAINT
#define WM_PAINT 0x000F
#endif
#ifndef WM_SIZE
#define WM_SIZE 0x0005
#endif
#ifndef WM_KEYDOWN
#define WM_KEYDOWN 0x0100
#endif
#ifndef WM_KEYUP
#define WM_KEYUP 0x0101
#endif
#ifndef WM_CHAR
#define WM_CHAR 0x0102
#endif
#ifndef WM_LBUTTONDOWN
#define WM_LBUTTONDOWN 0x0201
#endif
#ifndef WM_LBUTTONUP
#define WM_LBUTTONUP 0x0202
#endif
#ifndef WM_LBUTTONDBLCLK
#define WM_LBUTTONDBLCLK 0x0203
#endif
#ifndef WM_RBUTTONDOWN
#define WM_RBUTTONDOWN 0x0204
#endif
#ifndef WM_RBUTTONUP
#define WM_RBUTTONUP 0x0205
#endif
#ifndef WM_RBUTTONDBLCLK
#define WM_RBUTTONDBLCLK 0x0206
#endif
#ifndef WM_SETCURSOR
#define WM_SETCURSOR 0x0020
#endif
#ifndef WM_COMMAND
#define WM_COMMAND 0x0111
#endif
#ifndef WM_SYSCOMMAND
#define WM_SYSCOMMAND 0x0112
#endif
#ifndef WM_DRAWITEM
#define WM_DRAWITEM 0x002B
#endif
#ifndef WM_IME_ENDCOMPOSITION
#define WM_IME_ENDCOMPOSITION 0x010E
#endif
#ifndef WM_IME_COMPOSITION
#define WM_IME_COMPOSITION 0x010F
#endif
#ifndef WM_IME_SETCONTEXT
#define WM_IME_SETCONTEXT 0x0281
#endif
#ifndef WM_IME_NOTIFY
#define WM_IME_NOTIFY 0x0282
#endif
#ifndef WM_INPUTLANGCHANGE
#define WM_INPUTLANGCHANGE 0x0051
#endif
#ifndef WM_MOUSEMOVE
#define WM_MOUSEMOVE 0x0200
#endif
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

#ifndef VK_SHIFT
#define VK_SHIFT 0x10
#endif
#ifndef VK_CONTROL
#define VK_CONTROL 0x11
#endif
#ifndef VK_MENU
#define VK_MENU 0x12
#endif
#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1B
#endif
#ifndef VK_RETURN
#define VK_RETURN 0x0D
#endif
#ifndef VK_TAB
#define VK_TAB 0x09
#endif
#ifndef VK_BACK
#define VK_BACK 0x08
#endif
#ifndef VK_SPACE
#define VK_SPACE 0x20
#endif
#ifndef VK_LEFT
#define VK_LEFT 0x25
#endif
#ifndef VK_UP
#define VK_UP 0x26
#endif
#ifndef VK_RIGHT
#define VK_RIGHT 0x27
#endif
#ifndef VK_DOWN
#define VK_DOWN 0x28
#endif
#ifndef VK_F1
#define VK_F1 0x70
#endif
#ifndef VK_F4
#define VK_F4 0x73
#endif
#ifndef VK_F11
#define VK_F11 0x7A
#endif
#ifndef VK_F12
#define VK_F12 0x7B
#endif
#ifndef VK_F10
#define VK_F10 0x79
#endif
#ifndef VK_PRIOR
#define VK_PRIOR 0x21
#endif
#ifndef VK_NEXT
#define VK_NEXT 0x22
#endif
#ifndef VK_DELETE
#define VK_DELETE 0x2E
#endif
#ifndef VK_INSERT
#define VK_INSERT 0x2D
#endif
#ifndef VK_NUMPAD0
#define VK_NUMPAD0 0x60
#endif
#ifndef VK_NUMPAD9
#define VK_NUMPAD9 0x69
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif
#ifndef VK_SUBTRACT
#define VK_SUBTRACT 0x6D
#endif
#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS 0xBB
#endif
#ifndef VK_ADD
#define VK_ADD 0x6B
#endif
#ifndef VK_OEM_4
#define VK_OEM_4 0xDB
#endif
#ifndef VK_OEM_6
#define VK_OEM_6 0xDD
#endif
#ifndef VK_OEM_7
#define VK_OEM_7 0xDE
#endif
#ifndef VK_SNAPSHOT
#define VK_SNAPSHOT 0x2C
#endif

#ifndef IDC_ARROW
#define IDC_ARROW ((LPCSTR)32512)
#endif
#ifndef TRANSPARENT
#define TRANSPARENT 1
#endif
#ifndef OPAQUE
#define OPAQUE 2
#endif
#ifndef HORZRES
#define HORZRES 8
#endif
#ifndef VERTRES
#define VERTRES 10
#endif
#ifndef BITSPIXEL
#define BITSPIXEL 12
#endif
#ifndef VREFRESH
#define VREFRESH 116
#endif
#ifndef CP_ACP
#define CP_ACP 0
#endif
#ifndef VER_MINORVERSION
#define VER_MINORVERSION 0x0000001
#endif
#ifndef VER_MAJORVERSION
#define VER_MAJORVERSION 0x0000002
#endif
#ifndef VER_GREATER_EQUAL
#define VER_GREATER_EQUAL 3
#endif
#ifndef VER_SET_CONDITION
#define VER_SET_CONDITION(_mask, _type, _op) ((_mask) |= 0ULL)
#endif

#ifndef WS_OVERLAPPEDWINDOW
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#endif
#ifndef SW_SHOW
#define SW_SHOW 5
#endif
#ifndef SWP_SHOWWINDOW
#define SWP_SHOWWINDOW 0x0040
#endif
#ifndef WM_SYSKEYDOWN
#define WM_SYSKEYDOWN 0x0104
#endif
#ifndef WM_SYSKEYUP
#define WM_SYSKEYUP 0x0105
#endif
#ifndef HWND_TOPMOST
#define HWND_TOPMOST ((HWND)(LONG_PTR)-1)
#endif
#ifndef HWND_NOTOPMOST
#define HWND_NOTOPMOST ((HWND)(LONG_PTR)-2)
#endif

#ifndef GWL_STYLE
#define GWL_STYLE (-16)
#endif

#ifndef HKEY_LOCAL_MACHINE
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#endif
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2L
#endif
#ifndef ERROR_OUTOFMEMORY
#define ERROR_OUTOFMEMORY 14L
#endif
#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? (HRESULT)(x) : (HRESULT)(((x) & 0x0000FFFF) | (7 << 16) | 0x80000000))
#endif

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

#ifndef ZeroMemory
#define ZeroMemory(Destination, Length) std::memset((Destination), 0, (Length))
#endif
#ifndef CopyMemory
#define CopyMemory(Destination, Source, Length) std::memcpy((Destination), (Source), (Length))
#endif
#ifndef MoveMemory
#define MoveMemory(Destination, Source, Length) std::memmove((Destination), (Source), (Length))
#endif
#ifndef REG_SZ
#define REG_SZ 1
#endif
#ifndef REG_BINARY
#define REG_BINARY 3
#endif
#ifndef REG_DWORD
#define REG_DWORD 4
#endif
#ifndef KEY_READ
#define KEY_READ 0x20019
#endif
#ifndef KEY_WRITE
#define KEY_WRITE 0x20006
#endif
#ifndef ENUM_CURRENT_SETTINGS
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#endif
#ifndef GWL_USERDATA
#define GWL_USERDATA (-21)
#endif
#ifndef GWLP_USERDATA
#define GWLP_USERDATA (-21)
#endif
#ifndef GMEM_FIXED
#define GMEM_FIXED 0x0000
#endif
#ifndef PM_NOREMOVE
#define PM_NOREMOVE 0x0000
#endif
#ifndef PM_REMOVE
#define PM_REMOVE 0x0001
#endif
#ifndef WS_POPUP
#define WS_POPUP 0x80000000L
#endif
#ifndef WS_VISIBLE
#define WS_VISIBLE 0x10000000L
#endif
#ifndef CW_USEDEFAULT
#define CW_USEDEFAULT ((int)0x80000000)
#endif

struct FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
};


struct DEVMODEA {
  BYTE dmDeviceName[32];
  WORD dmSpecVersion;
  WORD dmDriverVersion;
  WORD dmSize;
  WORD dmDriverExtra;
  DWORD dmFields;
  LONG dmPositionX;
  LONG dmPositionY;
  DWORD dmDisplayOrientation;
  DWORD dmDisplayFixedOutput;
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  BYTE dmFormName[32];
  WORD dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  DWORD dmDisplayFlags;
  DWORD dmDisplayFrequency;
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
};

using DEVMODE = DEVMODEA;
using PDEVMODE = DEVMODEA*;
using LPDEVMODEA = DEVMODEA*;
using PDEVMODEA = DEVMODEA*;

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
using _SYSTEMTIME = SYSTEMTIME;

union LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  long long QuadPart;
};
using _LARGE_INTEGER = LARGE_INTEGER;

union ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  unsigned long long QuadPart;
};

struct POINT {
  LONG x;
  LONG y;
};

struct POINTL {
  LONG x;
  LONG y;
};

struct SIZE {
  LONG cx;
  LONG cy;
};

struct RECT {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
};

struct SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
};

struct OVERLAPPED {
  ULONG_PTR Internal;
  ULONG_PTR InternalHigh;
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    };
    LPVOID Pointer;
  };
  HANDLE hEvent;
};

struct WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  CHAR cFileName[MAX_PATH];
  CHAR cAlternateFileName[14];
};


struct CANDIDATELIST {
  DWORD dwSize;
  DWORD dwStyle;
  DWORD dwCount;
  DWORD dwSelection;
  DWORD dwPageStart;
  DWORD dwPageSize;
  DWORD dwOffset[1];
};

using LPCANDIDATELIST = CANDIDATELIST*;


struct RGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
};

struct PALETTEENTRY {
  BYTE peRed;
  BYTE peGreen;
  BYTE peBlue;
  BYTE peFlags;
};

struct BITMAPINFOHEADER {
  DWORD biSize;
  LONG biWidth;
  LONG biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG biXPelsPerMeter;
  LONG biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
};

struct BITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
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
  RECT rcPaint;
  BOOL fRestore;
  BOOL fIncUpdate;
  BYTE rgbReserved[32];
};

using WNDPROC = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

struct WNDCLASSEXA {
  UINT cbSize;
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
  HICON hIconSm;
};

struct WNDCLASSA {
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

using LPSECURITY_ATTRIBUTES = SECURITY_ATTRIBUTES*;
using WNDCLASS = WNDCLASSA;
using LPWNDCLASSA = WNDCLASSA*;
using LPWNDCLASS = WNDCLASSA*;

struct BITMAPFILEHEADER {
  WORD bfType;
  DWORD bfSize;
  WORD bfReserved1;
  WORD bfReserved2;
  DWORD bfOffBits;
};

struct DRAWITEMSTRUCT {
  UINT CtlType;
  UINT CtlID;
  UINT itemID;
  UINT itemAction;
  UINT itemState;
  HWND hwndItem;
  HDC hDC;
  RECT rcItem;
  ULONG_PTR itemData;
};
using LPDRAWITEMSTRUCT = DRAWITEMSTRUCT*;

struct OSVERSIONINFOEXA {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  CHAR szCSDVersion[128];
  WORD wServicePackMajor;
  WORD wServicePackMinor;
  WORD wSuiteMask;
  BYTE wProductType;
  BYTE wReserved;
};
using OSVERSIONINFOEX = OSVERSIONINFOEXA;
using LPOSVERSIONINFOEXA = OSVERSIONINFOEXA*;
using LPOSVERSIONINFOEX = OSVERSIONINFOEXA*;

using LPTHREAD_START_ROUTINE = DWORD (WINAPI*)(LPVOID lpThreadParameter);

#ifdef __cplusplus
extern "C" {
#endif

int MessageBoxA(HWND, LPCSTR, LPCSTR, UINT);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);

DWORD GetTickCount(void);
DWORD GetLastError(void);
void GetLocalTime(SYSTEMTIME* lpSystemTime);
void Sleep(DWORD);

HANDLE CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, OVERLAPPED*);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, OVERLAPPED*);
BOOL CloseHandle(HANDLE);
BOOL DeleteFileA(LPCSTR);
BOOL DeleteFileW(LPCWSTR);
BOOL SetFileAttributesA(LPCSTR, DWORD);
DWORD GetFileAttributesA(LPCSTR);
BOOL CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists);

DWORD SetFilePointer(HANDLE, LONG, LPLONG, DWORD);
BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER, LARGE_INTEGER*, DWORD);
BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER*);
DWORD GetFileSize(HANDLE, LPDWORD);

BOOL SetCurrentDirectoryA(LPCSTR);
BOOL SetCurrentDirectoryW(LPCWSTR);
DWORD GetCurrentDirectoryA(DWORD, LPSTR);
DWORD GetCurrentDirectoryW(DWORD, LPWSTR);

HANDLE FindFirstFileA(LPCSTR, WIN32_FIND_DATAA*);
BOOL FindNextFileA(HANDLE, WIN32_FIND_DATAA*);
BOOL FindClose(HANDLE);

DWORD GetModuleFileNameA(HMODULE, LPSTR, DWORD);
DWORD GetFullPathNameA(LPCSTR, DWORD, LPSTR, LPSTR*);
HMODULE GetModuleHandleA(LPCSTR);
HMODULE LoadLibraryA(LPCSTR);
BOOL FreeLibrary(HMODULE);
FARPROC GetProcAddress(HMODULE, LPCSTR);
HRSRC FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
HGLOBAL LoadResource(HMODULE hModule, HRSRC hResInfo);
DWORD SizeofResource(HMODULE hModule, HRSRC hResInfo);
LPVOID LockResource(HGLOBAL hResData);

BOOL QueryPerformanceCounter(LARGE_INTEGER*);
BOOL QueryPerformanceFrequency(LARGE_INTEGER*);

BOOL GetMessageA(MSG*, HWND, UINT, UINT);
BOOL PeekMessageA(MSG*, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageA(const MSG*);
void PostQuitMessage(int);

HDC BeginPaint(HWND, PAINTSTRUCT*);
BOOL EndPaint(HWND, const PAINTSTRUCT*);
BOOL FillRect(HDC, const RECT*, HBRUSH);
BOOL SetRect(RECT* lprc, int xLeft, int yTop, int xRight, int yBottom);
BOOL IntersectRect(RECT* lprcDst, const RECT* lprcSrc1, const RECT* lprcSrc2);
BOOL AdjustWindowRect(RECT* lpRect, DWORD dwStyle, BOOL bMenu);
HCURSOR SetCursor(HCURSOR hCursor);
HGDIOBJ GetStockObject(int i);
BOOL DeleteObject(HGDIOBJ ho);
HBITMAP LoadBitmapA(HINSTANCE hInstance, LPCSTR lpBitmapName);
HCURSOR LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
HICON LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
HACCEL LoadAcceleratorsA(HINSTANCE hInstance, LPCSTR lpTableName);
int TranslateAcceleratorA(HWND hWnd, HACCEL hAccTable, MSG* lpMsg);
BOOL DestroyAcceleratorTable(HACCEL hAccel);
HDC GetDC(HWND hWnd);
int ReleaseDC(HWND hWnd, HDC hDC);
BOOL SetDeviceGammaRamp(HDC hDC, LPVOID lpRamp);
HDC CreateDCA(LPCSTR pwszDriver, LPCSTR pwszDevice, LPCSTR pszPort, const DEVMODEA* pdm);
HDC CreateCompatibleDC(HDC hdc);
BOOL DeleteDC(HDC hdc);
int GetDeviceCaps(HDC hdc, int index);
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h);
BOOL StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop);
HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset);
HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);
COLORREF SetTextColor(HDC hdc, COLORREF color);
COLORREF SetBkColor(HDC hdc, COLORREF color);
int SetBkMode(HDC hdc, int mode);
BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c);
BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, SIZE* pSize);

ATOM RegisterClassA(const WNDCLASSA*);
ATOM RegisterClassExA(const WNDCLASSEXA*);
BOOL UnregisterClassA(LPCSTR, HINSTANCE);
HWND CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
BOOL DestroyWindow(HWND);
BOOL ShowWindow(HWND, int);
BOOL UpdateWindow(HWND);
BOOL SetWindowTextA(HWND, LPCSTR);
HWND SetFocus(HWND);
HWND GetFocus(void);
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL CloseWindow(HWND hWnd);
HWND FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName);
LPSTR GetCommandLineA(void);
BOOL AllocConsole(void);
BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
BOOL SetMenu(HWND hWnd, HMENU hMenu);
HMENU GetMenu(HWND hWnd);
BOOL GetClientRect(HWND, RECT*);
BOOL GetWindowRect(HWND, RECT*);
BOOL ClientToScreen(HWND, POINT*);
BOOL ScreenToClient(HWND, POINT*);
BOOL PtInRect(const RECT*, POINT);

short GetAsyncKeyState(int);
short GetKeyState(int);

int MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int);
int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL);

LPSTR lstrcpyA(LPSTR, LPCSTR);
LPSTR lstrcpynA(LPSTR, LPCSTR, int);
LPSTR lstrcatA(LPSTR, LPCSTR);
int lstrlenA(LPCSTR);
int lstrcmpA(LPCSTR, LPCSTR);
int lstrcmpiA(LPCSTR, LPCSTR);
LPSTR CharNextA(LPCSTR lpsz);
LPSTR CharPrevA(LPCSTR lpszStart, LPCSTR lpszCurrent);

DWORD GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
BOOL WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR);

BOOL EnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode);
LONG ChangeDisplaySettingsA(DEVMODEA* lpDevMode, DWORD dwFlags);

LONG RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
LONG RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE* lpData, DWORD cbData);
LONG RegCloseKey(HKEY hKey);

LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM);
BOOL PostMessageA(HWND, UINT, WPARAM, LPARAM);
LONG GetWindowLongA(HWND, int);
LONG SetWindowLongA(HWND, int, LONG);
LONG_PTR GetWindowLongPtrA(HWND, int);
LONG_PTR SetWindowLongPtrA(HWND, int, LONG_PTR);

HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
HGLOBAL GlobalFree(HGLOBAL hMem);

HWND GetDlgItem(HWND hDlg, int nIDDlgItem);
int GetWindowTextA(HWND, LPSTR, int);
int GetWindowTextW(HWND, LPWSTR, int);

HKL GetKeyboardLayout(DWORD idThread);
BOOL GetKeyboardLayoutNameA(LPSTR pwszKLID);
UINT ImmGetDescriptionA(HKL hKL, LPSTR lpszDescription, UINT uBufLen);
HIMC ImmCreateContext(void);
BOOL ImmDestroyContext(HIMC hIMC);
HIMC ImmGetContext(HWND hWnd);
BOOL ImmReleaseContext(HWND hWnd, HIMC hIMC);
HIMC ImmAssociateContext(HWND hWnd, HIMC hIMC);
HWND ImmGetDefaultIMEWnd(HWND hWnd);
BOOL ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence);
BOOL ImmSetConversionStatus(HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence);
BOOL ImmSetOpenStatus(HIMC hIMC, BOOL fOpen);
BOOL ImmGetOpenStatus(HIMC hIMC);
void OutputDebugStringA(LPCSTR lpOutputString);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
BOOL VerifyVersionInfoA(LPOSVERSIONINFOEXA lpVersionInfo, DWORD dwTypeMask, DWORDLONG dwlConditionMask);


#ifdef __cplusplus
}
#endif

#ifndef TEXT
#define TEXT(x) x
#endif
#ifndef _T
#define _T(x) x
#endif

#ifndef MessageBox
#define MessageBox MessageBoxA
#endif
#ifndef CreateFile
#define CreateFile CreateFileA
#endif
#ifndef DeleteFile
#define DeleteFile DeleteFileA
#endif
#ifndef SetFileAttributes
#define SetFileAttributes SetFileAttributesA
#endif
#ifndef GetFileAttributes
#define GetFileAttributes GetFileAttributesA
#endif
#ifndef SetCurrentDirectory
#define SetCurrentDirectory SetCurrentDirectoryA
#endif
#ifndef GetCurrentDirectory
#define GetCurrentDirectory GetCurrentDirectoryA
#endif
#ifndef GetModuleFileName
#define GetModuleFileName GetModuleFileNameA
#endif
#ifndef GetFullPathName
#define GetFullPathName GetFullPathNameA
#endif
#ifndef GetModuleHandle
#define GetModuleHandle GetModuleHandleA
#endif
#ifndef LoadLibrary
#define LoadLibrary LoadLibraryA
#endif
#ifndef RegisterClass
#define RegisterClass RegisterClassA
#endif
#ifndef RegisterClassEx
#define RegisterClassEx RegisterClassExA
#endif
#ifndef CreateWindowEx
#define CreateWindowEx CreateWindowExA
#endif
#ifndef DefWindowProc
#define DefWindowProc DefWindowProcA
#endif
#ifndef FindWindow
#define FindWindow FindWindowA
#endif
#ifndef GetCommandLine
#define GetCommandLine GetCommandLineA
#endif
#ifndef GetMessage
#define GetMessage GetMessageA
#endif
#ifndef PeekMessage
#define PeekMessage PeekMessageA
#endif
#ifndef DispatchMessage
#define DispatchMessage DispatchMessageA
#endif
#ifndef GetPrivateProfileString
#define GetPrivateProfileString GetPrivateProfileStringA
#endif
#ifndef WritePrivateProfileString
#define WritePrivateProfileString WritePrivateProfileStringA
#endif

#ifndef SendMessage
#define SendMessage SendMessageA
#endif
#ifndef PostMessage
#define PostMessage PostMessageA
#endif
#ifndef GetWindowLong
#define GetWindowLong GetWindowLongA
#endif
#ifndef SetWindowLong
#define SetWindowLong SetWindowLongA
#endif
#ifndef GetWindowLongPtr
#define GetWindowLongPtr GetWindowLongPtrA
#endif
#ifndef SetWindowLongPtr
#define SetWindowLongPtr SetWindowLongPtrA
#endif
#ifndef GetWindowText
#define GetWindowText GetWindowTextA
#endif
#ifndef LoadBitmap
#define LoadBitmap LoadBitmapA
#endif
#ifndef LoadCursor
#define LoadCursor LoadCursorA
#endif
#ifndef LoadIcon
#define LoadIcon LoadIconA
#endif
#ifndef LoadAccelerators
#define LoadAccelerators LoadAcceleratorsA
#endif
#ifndef TranslateAccelerator
#define TranslateAccelerator TranslateAcceleratorA
#endif
#ifndef TextOut
#define TextOut TextOutA
#endif
#ifndef GetTextExtentPoint32
#define GetTextExtentPoint32 GetTextExtentPoint32A
#endif
#ifndef CreateFont
#define CreateFont CreateFontA
#endif
#ifndef OutputDebugString
#define OutputDebugString OutputDebugStringA
#endif
#ifndef GetKeyboardLayoutName
#define GetKeyboardLayoutName GetKeyboardLayoutNameA
#endif
#ifndef VerifyVersionInfo
#define VerifyVersionInfo VerifyVersionInfoA
#endif
#ifndef lstrcpy
#define lstrcpy lstrcpyA
#endif
#ifndef lstrcpyn
#define lstrcpyn lstrcpynA
#endif
#ifndef lstrcat
#define lstrcat lstrcatA
#endif
#ifndef lstrlen
#define lstrlen lstrlenA
#endif
#ifndef lstrcmp
#define lstrcmp lstrcmpA
#endif
#ifndef lstrcmpi
#define lstrcmpi lstrcmpiA
#endif
#ifndef ImmGetDescription
#define ImmGetDescription ImmGetDescriptionA
#endif
#ifndef CharNext
#define CharNext CharNextA
#endif
#ifndef CharPrev
#define CharPrev CharPrevA
#endif
#ifndef EnumDisplaySettings
#define EnumDisplaySettings EnumDisplaySettingsA
#endif
#ifndef ChangeDisplaySettings
#define ChangeDisplaySettings ChangeDisplaySettingsA
#endif
#ifndef RegOpenKeyEx
#define RegOpenKeyEx RegOpenKeyExA
#endif
#ifndef RegQueryValueEx
#define RegQueryValueEx RegQueryValueExA
#endif
#ifndef RegSetValueEx
#define RegSetValueEx RegSetValueExA
#endif
