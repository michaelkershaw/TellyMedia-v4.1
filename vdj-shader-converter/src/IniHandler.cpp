#include "IniHandler.h"
#include <shlobj.h>

std::wstring IniHandler::GetOutputDirectory(const std::wstring& iniPath) {
    wchar_t buffer[MAX_PATH];
    DWORD result = GetPrivateProfileStringW(L"Settings", L"OutputDirectory", L"", 
                                             buffer, MAX_PATH, iniPath.c_str());
    if (result > 0) {
        return std::wstring(buffer);
    }
    return L"";
}

bool IniHandler::SaveOutputDirectory(const std::wstring& iniPath, const std::wstring& outputDir) {
    return WritePrivateProfileStringW(L"Settings", L"OutputDirectory", 
                                       outputDir.c_str(), iniPath.c_str()) != 0;
}

std::wstring IniHandler::GetDefaultIniPath() {
    wchar_t appDataPath[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath) == S_OK) {
        std::wstring iniPath = appDataPath;
        iniPath += L"\\VDJShaderConverter\\settings.ini";
        return iniPath;
    }
    return L"settings.ini";
}
