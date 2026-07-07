#pragma once

#include <string>
#include <windows.h>

class Logger {
public:
    static void Initialize();
    static void Log(const std::wstring& message);
    static void LogError(const std::wstring& message);
    static void LogInfo(const std::wstring& message);
    static void LogSuccess(const std::wstring& message);
    static std::wstring GetLogFilePath();
    
private:
    static std::wstring logFilePath;
    static HANDLE hLogFile;
    static CRITICAL_SECTION logCS;
};
