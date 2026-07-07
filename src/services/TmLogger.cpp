#include "tm/TmLogger.h"
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>

HANDLE           TmLogger::s_hFile  = nullptr;
CRITICAL_SECTION TmLogger::s_cs     = {};
bool             TmLogger::s_inited = false;

void TmLogger::Init()
{
    if (s_inited) return;
    InitializeCriticalSection(&s_cs);

    // Get this DLL's own path using an address inside the DLL itself
    wchar_t logPath[MAX_PATH] = {};
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&TmLogger::s_inited,
        &hSelf);

    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(hSelf, dllPath, MAX_PATH);

    // Replace DLL filename with log filename (same directory as DLL)
    wcscpy_s(logPath, dllPath);
    wchar_t* lastSlash = wcsrchr(logPath, L'\\');
    if (lastSlash) {
        wcscpy_s(lastSlash + 1, MAX_PATH - (DWORD)(lastSlash - logPath + 1), L"TellyMedia-v4_log.txt");
    } else {
        wcscpy_s(logPath, L"TellyMedia-v4_log.txt");
    }

    s_hFile = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    s_inited = true;
    Sep();
    Info("TellyMedia v4 logger initialized");
    Info("Log file: %S", logPath);
}

void TmLogger::Shutdown()
{
    if (!s_inited) return;
    EnterCriticalSection(&s_cs);
    if (s_hFile && s_hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(s_hFile);
        s_hFile = nullptr;
    }
    LeaveCriticalSection(&s_cs);
    DeleteCriticalSection(&s_cs);
    s_inited = false;
}

void TmLogger::Write(const char *level, const char *msg)
{
    if (!s_inited || !s_hFile || s_hFile == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st; GetLocalTime(&st);
    char line[2048];
    int n = _snprintf_s(line, _TRUNCATE,
        "[%02d:%02d:%02d.%03d] [%s] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level, msg);

    EnterCriticalSection(&s_cs);
    DWORD written = 0;
    if (n > 0) WriteFile(s_hFile, line, (DWORD)n, &written, nullptr);
    FlushFileBuffers(s_hFile);
    LeaveCriticalSection(&s_cs);
}

static void FormatVa(char *buf, size_t cap, const char *fmt, va_list args)
{
    _vsnprintf_s(buf, cap, _TRUNCATE, fmt, args);
}

void TmLogger::Info(const char *fmt, ...)
{
    char buf[1900]; va_list a; va_start(a, fmt); FormatVa(buf, sizeof(buf), fmt, a); va_end(a);
    Write("INFO", buf);
}
void TmLogger::Warn(const char *fmt, ...)
{
    char buf[1900]; va_list a; va_start(a, fmt); FormatVa(buf, sizeof(buf), fmt, a); va_end(a);
    Write("WARN", buf);
}
void TmLogger::Error(const char *fmt, ...)
{
    char buf[1900]; va_list a; va_start(a, fmt); FormatVa(buf, sizeof(buf), fmt, a); va_end(a);
    Write("ERROR", buf);
}
void TmLogger::HR(const char *tag, HRESULT hr)
{
    char buf[256];
    _snprintf_s(buf, _TRUNCATE, "%s -> HRESULT 0x%08X", tag, (unsigned)hr);
    Write(hr == S_OK ? "INFO" : "ERROR", buf);
}
void TmLogger::Sep()
{
    Write("----", "--------------------------------------------------");
}
