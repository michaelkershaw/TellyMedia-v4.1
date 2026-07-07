// TmLoggerMac.mm Ã¢â‚¬â€ macOS logger implementation
// Writes to ~/Library/Logs/TellyMedia-v4.log using pthread_mutex for thread safety.

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#include "tm/TmLogger.h"
#include <pthread.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cstring>

static FILE* s_logFile = nullptr;
static pthread_mutex_t s_logMutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_logInited = false;

void TmLogger::Init() {
    if (s_logInited) return;
    s_logInited = true;

    NSString* logDir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs"];
    NSString* logPath = [logDir stringByAppendingPathComponent:@"TellyMedia-v4.log"];

    // Ensure directory exists
    NSFileManager* fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:logDir]) {
        [fm createDirectoryAtPath:logDir withIntermediateDirectories:YES attributes:nil error:nil];
    }

    s_logFile = fopen([logPath UTF8String], "a");
    if (s_logFile) {
        fprintf(s_logFile, "\n================================================================\n");
        fprintf(s_logFile, "TellyMedia v4 - Session started\n");
        fflush(s_logFile);
    }
}

void TmLogger::Shutdown() {
    pthread_mutex_lock(&s_logMutex);
    if (s_logFile) {
        fprintf(s_logFile, "TellyMedia v4 - Session ended\n");
        fclose(s_logFile);
        s_logFile = nullptr;
    }
    pthread_mutex_unlock(&s_logMutex);
    s_logInited = false;
}

void TmLogger::Write(const char* level, const char* msg) {
    pthread_mutex_lock(&s_logMutex);
    if (s_logFile) {
        time_t now = time(nullptr);
        struct tm tm;
        localtime_r(&now, &tm);
        fprintf(s_logFile, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                level, msg);
        fflush(s_logFile);
    }
    pthread_mutex_unlock(&s_logMutex);
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

void TmLogger::Sep() {
    Write("----", "----------------------------------------------------------------");
}

#endif // VDJ_MAC
