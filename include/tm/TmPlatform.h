#pragma once

// ─── Cross-platform type definitions ──────────────────────────────────────────
// Provides Windows-compatible types on macOS so shared headers compile on both.

#if defined(__APPLE__) || defined(__MACOSX__)

// macOS
#define VDJ_MAC
#define VDJ_PLATFORM_MAC 1

#include <cstdint>
#include <cstddef>
#include <cstring>

// Windows type aliases for shared code
typedef int32_t  HRESULT;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef int32_t  LONG;
typedef uint32_t ULONG;
#ifndef __OBJC__
typedef int      BOOL;
#endif
typedef unsigned char BYTE;
typedef wchar_t  WCHAR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef char*   LPSTR;
typedef const char* LPCSTR;
typedef void*   HANDLE;
typedef void*   HWND;
typedef void*   HINSTANCE;
typedef void*   HMODULE;
typedef void*   HDC;
typedef void*   HBITMAP;
typedef void*   HFONT;
typedef void*   HBRUSH;
typedef void*   HPEN;
typedef unsigned long ULONG_PTR;
typedef long    LPARAM;
typedef unsigned long WPARAM;

typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *PRECT;

typedef struct tagPOINT {
    LONG x, y;
} POINT;

typedef struct tagSIZE {
    LONG cx, cy;
} SIZE;

// HRESULT constants
#define S_OK     ((HRESULT)0)
#define S_FALSE  ((HRESULT)1)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_FAIL    ((HRESULT)0x80004005L)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)

// BOOL values
#define TRUE  1
#define FALSE 0

// Macros
#define MAX_PATH 1024
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
#define swprintf_s swprintf
#define wcscpy_s(a,b,c) wcscpy(a,c)
#define wcsncpy_s(a,b,c,d) wcsncpy(a,c,d)
#define _TRUNCATE ((size_t)-1)
#define ZeroMemory(p,s) memset(p,0,s)

// COLORREF
typedef DWORD COLORREF;
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(c) ((BYTE)((c)))
#define GetGValue(c) ((BYTE)(((c)>>8)))
#define GetBValue(c) ((BYTE)((c)>>16))

// GUID
struct GUID {
    uint32_t  Data1;
    uint16_t  Data2;
    uint16_t  Data3;
    uint8_t   Data4[8];
};

inline bool operator==(const GUID& a, const GUID& b) {
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}
inline bool operator!=(const GUID& a, const GUID& b) {
    return !(a == b);
}

// Export
#define VDJ_EXPORT __attribute__((visibility("default")))
#define VDJ_API

#else

// Windows
#define VDJ_WIN
#define VDJ_PLATFORM_WIN 1

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VDJ_EXPORT __declspec(dllexport)
#define VDJ_API __stdcall

#endif

// ─── Shared constants ─────────────────────────────────────────────────────────
#define kShaderNameBufferChars 8192
