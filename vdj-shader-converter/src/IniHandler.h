#pragma once

#include <string>
#include <windows.h>

class IniHandler {
public:
    static std::wstring GetOutputDirectory(const std::wstring& iniPath);
    static bool SaveOutputDirectory(const std::wstring& iniPath, const std::wstring& outputDir);
    static std::wstring GetDefaultIniPath();
};
