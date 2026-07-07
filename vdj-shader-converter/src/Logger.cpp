#include "Logger.h"
#include <shlobj.h>
#include <time.h>

std::wstring Logger::logFilePath;
HANDLE Logger::hLogFile = INVALID_HANDLE_VALUE;
CRITICAL_SECTION Logger::logCS;

void Logger::Initialize() {
    InitializeCriticalSection(&logCS);
    
    // Get AppData directory
    wchar_t appDataPath[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath) == S_OK) {
        // Create log directory
        std::wstring logDir = appDataPath;
        logDir += L"\\VDJShaderConverter";
        CreateDirectoryW(logDir.c_str(), nullptr);
        
        // Create log file with timestamp
        logFilePath = logDir + L"\\converter.log";
        
        hLogFile = CreateFileW(logFilePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        
        if (hLogFile != INVALID_HANDLE_VALUE) {
            // Move to end of file
            SetFilePointer(hLogFile, 0, nullptr, FILE_END);
            
            // Write session header
            time_t now = time(nullptr);
            struct tm timeinfo;
            localtime_s(&timeinfo, &now);
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            
            std::wstring header = L"\n========================================\n";
            header += L"VDJShader Converter - New Session\n";
            header += L"Time: ";
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, timeStr, -1, nullptr, 0);
            std::wstring wideTime(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, timeStr, -1, &wideTime[0], size_needed);
            header += wideTime;
            header += L"\n========================================\n";
            
            DWORD written;
            WriteFile(hLogFile, header.c_str(), (DWORD)(header.size() * sizeof(wchar_t)), &written, nullptr);
            FlushFileBuffers(hLogFile);
        }
    }
}

void Logger::Log(const std::wstring& message) {
    EnterCriticalSection(&logCS);
    
    if (hLogFile != INVALID_HANDLE_VALUE) {
        // Add timestamp
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        
        std::wstring logLine = L"[";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, timeStr, -1, nullptr, 0);
        std::wstring wideTime(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, timeStr, -1, &wideTime[0], size_needed);
        logLine += wideTime;
        logLine += L"] ";
        logLine += message;
        logLine += L"\n";
        
        DWORD written;
        WriteFile(hLogFile, logLine.c_str(), (DWORD)(logLine.size() * sizeof(wchar_t)), &written, nullptr);
        FlushFileBuffers(hLogFile);
    }
    
    LeaveCriticalSection(&logCS);
}

void Logger::LogError(const std::wstring& message) {
    Log(L"[ERROR] " + message);
}

void Logger::LogInfo(const std::wstring& message) {
    Log(L"[INFO] " + message);
}

void Logger::LogSuccess(const std::wstring& message) {
    Log(L"[SUCCESS] " + message);
}

std::wstring Logger::GetLogFilePath() {
    return logFilePath;
}
