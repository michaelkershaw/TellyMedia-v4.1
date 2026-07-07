#pragma once
#include "tm/TmPlatform.h"

// Thread-safe logger. Writes to platform-appropriate log location.
// Windows: %LOCALAPPDATA%\VirtualDJ\TellyMedia-v4_log.txt
// macOS: ~/Library/Logs/TellyMedia-v4.log
class TmLogger
{
public:
    static void Init();
    static void Shutdown();

    static void Info (const char *fmt, ...);
    static void Warn (const char *fmt, ...);
    static void Error(const char *fmt, ...);
    static void HR   (const char *tag, HRESULT hr);
    static void Sep  ();

private:
    static void Write(const char *level, const char *msg);
#if defined(VDJ_WIN)
    static HANDLE           s_hFile;
    static CRITICAL_SECTION s_cs;
#elif defined(VDJ_MAC)
    static void*            s_hFile;  // FILE*
    static void*            s_cs;     // pthread_mutex_t*
#endif
    static bool             s_inited;
};

#define TM_INFO(...)  TmLogger::Info(__VA_ARGS__)
#define TM_WARN(...)  TmLogger::Warn(__VA_ARGS__)
#define TM_ERR(...)   TmLogger::Error(__VA_ARGS__)
#define TM_ERROR(...) TmLogger::Error(__VA_ARGS__)
#define TM_HR(t,hr)   TmLogger::HR(t, hr)
#define TM_SEP()      TmLogger::Sep()
