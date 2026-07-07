// TmLoggerMac.mm — macOS logger implementation
// Writes to ~/Library/Logs/TellyMedia-v4.log using pthread_mutex for thread safety.

#if defined(VDJ_MAC)

#import <Foundation/Foundation.h>
#include "tm/TmLogger.h"
#include <pthread.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cstring>

static FILE* s_file = nullptr;
static pthread_mutex_t s_cs = PTHREAD_MUTEX_INITIALIZER;
static bool s_initialized = false;

void TmLogger::Init() {
    if (s_initialized) return;
    s_initialized = true;

    NSString* logDir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs"];
    NSString* logPath = [logDir stringByAppendingPathComponent:@"TellyMedia-v4.log"];

    // Ensure directory exists
    NSFileManager* fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:logDir]) {
        [fm createDirectoryAtPath:logDir withIntermediateDirectories:YES attributes:nil error:nil];
    }

    s_file = fopen([logPath UTF8String], "a");
    if (s_file) {
        fprintf(s_file, "\n═══════════════════════════════════════════════════════════════\n");
        fprintf(s_file, "TellyMedia v4 — Session started\n");
        fflush(s_file);
    }
}

void TmLogger::Shutdown() {
    pthread_mutex_lock(&s_cs);
    if (s_file) {
        fprintf(s_file, "TellyMedia v4 — Session ended\n");
        fclose(s_file);
        s_file = nullptr;
    }
    pthread_mutex_unlock(&s_cs);
    s_initialized = false;
}

void TmLogger::Write(const char* level, const char* msg) {
    pthread_mutex_lock(&s_cs);
    if (s_file) {
        time_t now = time(nullptr);
        struct tm tm;
        localtime_r(&now, &tm);
        fprintf(s_file, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                level, msg);
        fflush(s_file);
    }
    pthread_mutex_unlock(&s_cs);
}

void TmLogger::Info(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Write("INFO", buf);
}

void TmLogger::Warn(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Write("WARN", buf);
}

void TmLogger::Error(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Write("ERROR", buf);
}

void TmLogger::HR(const char* tag, HRESULT hr) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: HR=0x%08X", tag, (unsigned)hr);
    Write("HR", buf);
}

#endif // VDJ_MAC
