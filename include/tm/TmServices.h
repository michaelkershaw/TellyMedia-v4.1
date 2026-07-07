#pragma once
#include "tm/TmPlatform.h"
#if defined(VDJ_WIN)
#include <winhttp.h>
#endif
#include "tm/TmTypes.h"

// Lightweight HTTP + JSON helpers.
namespace TmHttp {
    bool PostJson(const wchar_t* host, uint16_t port, bool https,
                  const wchar_t* path, const char* jsonBody,
                  char* response, int maxResponse);
    bool GetText(const wchar_t* host, uint16_t port, bool https,
                 const wchar_t* path, char* response, int maxResponse);
}

namespace TmJson {
    bool GetString(const char* json, const char* key, char* out, int maxOut);
    bool GetInt(const char* json, const char* key, int* out);
}

// Account / device licensing. Faithful port of v2 behaviour, cleaned up.
namespace TmLicense {
    void Init(LicenseState* st);
    void GetMachineId(char* out, int maxOut);
    bool Login(LicenseState* st, const wchar_t* email, const wchar_t* password);
    bool Validate(LicenseState* st);
    bool ActivateDevice(LicenseState* st);
    void Logout(LicenseState* st);
    void SaveToRegistry(const LicenseState* st);
    void LoadFromRegistry(LicenseState* st);
    void SaveSecureCredentials(const LicenseState* st);
    void LoadSecureCredentials(LicenseState* st);
}

// ClickSend SMS integration (outbound send + inbound poll).
namespace TmSms {
    void LoadSettings(ClickSendSettings* s);
    void SaveSettings(const ClickSendSettings* s);
    void GetIniPath(wchar_t* out, int maxLen);
    bool Send(const ClickSendSettings* s, const char* message, const char* toNumber);
}

// Background update checker.
namespace TmUpdates {
    // Returns true and fills latestVersion if a newer version is available.
    bool Check(const char* currentVersion, char* latestVersion, int maxLen);
}
