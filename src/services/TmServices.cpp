#include "tm/TmServices.h"
#include "tm/TmLogger.h"
#include <winhttp.h>
#include <wincred.h>
#include <string>
#include <cstring>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
constexpr const wchar_t* kLicenseRegKey = L"SOFTWARE\\TellyMedia";
constexpr const wchar_t* kCredentialTarget = L"TellyMedia_AuthToken";
constexpr const char* kLicensePlugin = "tellymedia-reborn";

static bool CredentialStore(const wchar_t* targetName, const wchar_t* username, const wchar_t* password)
{
    if (!targetName || !username || !password) return false;

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(targetName);
    cred.UserName = const_cast<wchar_t*>(username);
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.CredentialBlobSize = (DWORD)((wcslen(password) + 1) * sizeof(wchar_t));
    cred.CredentialBlob = (LPBYTE)password;
    return CredWriteW(&cred, 0) == TRUE;
}

static bool CredentialRead(const wchar_t* targetName, wchar_t* username, int usernameLen, wchar_t* password, int passwordLen)
{
    if (!targetName || !username || !password || usernameLen <= 0 || passwordLen <= 0) return false;

    PCREDENTIALW pcred = nullptr;
    if (!CredReadW(targetName, CRED_TYPE_GENERIC, 0, &pcred)) {
        username[0] = L'\0';
        password[0] = L'\0';
        return false;
    }

    if (pcred->UserName) {
        wcsncpy_s(username, usernameLen, pcred->UserName, _TRUNCATE);
    } else {
        username[0] = L'\0';
    }

    if (pcred->CredentialBlob && pcred->CredentialBlobSize > 0) {
        int charCount = (int)(pcred->CredentialBlobSize / sizeof(wchar_t));
        if (charCount >= passwordLen) charCount = passwordLen - 1;
        if (charCount < 0) charCount = 0;
        wcsncpy_s(password, passwordLen, (const wchar_t*)pcred->CredentialBlob, charCount);
        password[charCount] = L'\0';
    } else {
        password[0] = L'\0';
    }

    CredFree(pcred);
    return true;
}

static bool CredentialDelete(const wchar_t* targetName)
{
    if (!targetName) return false;
    return CredDeleteW(targetName, CRED_TYPE_GENERIC, 0) == TRUE;
}

static bool HttpRequest(const wchar_t* url, const char* method, const char* body, char* response, int maxResponse)
{
    if (!url || !method || !response || maxResponse <= 0) return false;
    response[0] = '\0';

    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);

    wchar_t host[256] = {};
    wchar_t path[512] = {};
    wchar_t extra[256] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = _countof(path);
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = _countof(extra);

    if (!WinHttpCrackUrl(url, 0, 0, &uc)) {
        TM_WARN("HTTP: WinHttpCrackUrl failed for %S", url);
        return false;
    }

    std::wstring fullPath = path;
    if (extra[0]) fullPath += extra;

    HINTERNET hSession = WinHttpOpen(L"TellyMedia/4.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 30000, 30000, 30000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

    const wchar_t* methodW = L"GET";
    if (_stricmp(method, "POST") == 0) {
        methodW = L"POST";
    } else if (_stricmp(method, "PUT") == 0) {
        methodW = L"PUT";
    } else if (_stricmp(method, "DELETE") == 0) {
        methodW = L"DELETE";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, methodW, fullPath.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (uc.nScheme == INTERNET_SCHEME_HTTPS) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    }

    const wchar_t* contentType = L"Content-Type: application/json";
    DWORD bodyLen = body ? (DWORD)strlen(body) : 0;
    BOOL sent = WinHttpSendRequest(hRequest, bodyLen ? contentType : WINHTTP_NO_ADDITIONAL_HEADERS,
                                   bodyLen ? (DWORD)-1L : 0,
                                   (LPVOID)body, bodyLen, bodyLen, 0);
    if (!sent) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD bytesRead = 0;
    int totalRead = 0;
    while (totalRead < maxResponse - 1 && WinHttpReadData(hRequest, response + totalRead, maxResponse - totalRead - 1, &bytesRead) && bytesRead > 0) {
        totalRead += (int)bytesRead;
        if (totalRead >= maxResponse - 1) break;
    }
    response[totalRead] = '\0';

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return totalRead > 0;
}

static void ClearLicenseState(LicenseState* st)
{
    if (!st) return;
    ZeroMemory(st, sizeof(*st));
    st->status = LIC_UNCHECKED;
}
}

namespace TmHttp {

bool PostJson(const wchar_t* host, INTERNET_PORT port, bool https,
              const wchar_t* path, const char* jsonBody,
              char* response, int maxResponse)
{
    if (!host || !path || !response || maxResponse <= 0) return false;

    wchar_t url[1024] = {};
    swprintf_s(url, L"%s://%s:%u%s", https ? L"https" : L"http", host, (unsigned)port, path);
    return HttpRequest(url, "POST", jsonBody, response, maxResponse);
}

bool GetText(const wchar_t* host, INTERNET_PORT port, bool https,
             const wchar_t* path, char* response, int maxResponse)
{
    if (!host || !path || !response || maxResponse <= 0) return false;

    wchar_t url[1024] = {};
    swprintf_s(url, L"%s://%s:%u%s", https ? L"https" : L"http", host, (unsigned)port, path);
    return HttpRequest(url, "GET", nullptr, response, maxResponse);
}

}

namespace TmJson {

bool GetString(const char* json, const char* key, char* out, int maxOut)
{
    if (!json || !key || !out || maxOut <= 0) return false;
    out[0] = '\0';

    char pattern[96] = {};
    sprintf_s(pattern, "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) ++p;

    if (*p == '"') {
        ++p;
        const char* end = p;
        while (*end && *end != '"') ++end;
        size_t len = (size_t)(end - p);
        if (len >= (size_t)maxOut) len = (size_t)maxOut - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        return true;
    }

    const char* end = p;
    while (*end && *end != ',' && *end != '}' && *end != '\r' && *end != '\n') ++end;
    size_t len = (size_t)(end - p);
    if (len >= (size_t)maxOut) len = (size_t)maxOut - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return len > 0;
}

bool GetInt(const char* json, const char* key, int* out)
{
    if (!json || !key || !out) return false;
    char buf[64] = {};
    if (!GetString(json, key, buf, _countof(buf))) return false;
    *out = atoi(buf);
    return true;
}

}

namespace TmLicense {

void Init(LicenseState* st)
{
    ClearLicenseState(st);
    if (!st) return;
    st->maxActivations = 0;
    st->currentActivations = 0;
    st->maxSubLicenses = 0;
    st->currentSubLicenses = 0;
    st->savePassword = false;
    st->isSharedLicense = false;
    st->licensed = false;
}

void GetMachineId(char* out, int maxOut)
{
    if (!out || maxOut <= 0) return;
    out[0] = '\0';

    char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(computerName);
    if (GetComputerNameA(computerName, &size)) {
        DWORD volumeSerial = 0;
        GetVolumeInformationA("C:\\", nullptr, 0, &volumeSerial, nullptr, nullptr, nullptr, 0);
        sprintf_s(out, maxOut, "%s-%08X", computerName, volumeSerial);
    } else {
        strcpy_s(out, maxOut, "UNKNOWN-DEVICE");
    }
}

void SaveSecureCredentials(const LicenseState* st)
{
    if (!st) return;

    if (st->savePassword && st->userEmail[0] != L'\0' && st->authToken[0] != '\0') {
        wchar_t wAuthToken[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, st->authToken, -1, wAuthToken, _countof(wAuthToken));
        if (CredentialStore(kCredentialTarget, st->userEmail, wAuthToken)) {
            TM_INFO("License: Saved credentials to Windows Credential Manager");
        } else {
            TM_ERR("License: Failed to save credentials to Credential Manager");
        }
    } else if (!st->savePassword) {
        CredentialDelete(kCredentialTarget);
        TM_INFO("License: Removed credentials from secure storage (user opted not to save)");
    }
}

void LoadSecureCredentials(LicenseState* st)
{
    if (!st) return;

    wchar_t email[256] = {}, authToken[512] = {};
    if (CredentialRead(kCredentialTarget, email, _countof(email), authToken, _countof(authToken))) {
        wcscpy_s(st->userEmail, email);
        WideCharToMultiByte(CP_UTF8, 0, authToken, -1, st->authToken, sizeof(st->authToken), nullptr, nullptr);
        st->savePassword = true;
        TM_INFO("License: Loaded credentials from Windows Credential Manager — email=%S", email);
    } else {
        TM_INFO("License: No credentials found in Credential Manager");
    }
}

void SaveToRegistry(const LicenseState* st)
{
    if (!st) return;

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kLicenseRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "LicenseKey", 0, REG_SZ, (const BYTE*)st->licenseKey, (DWORD)strlen(st->licenseKey) + 1);
        DWORD statusVal = (DWORD)st->status;
        RegSetValueExA(hKey, "LicenseStatus", 0, REG_DWORD, (const BYTE*)&statusVal, sizeof(DWORD));
        RegSetValueExA(hKey, "LicenseExpiry", 0, REG_SZ, (const BYTE*)st->licExpiry, (DWORD)strlen(st->licExpiry) + 1);
        RegSetValueExA(hKey, "TokenExpiry", 0, REG_SZ, (const BYTE*)st->tokenExpiry, (DWORD)strlen(st->tokenExpiry) + 1);
        RegSetValueExA(hKey, "LicenseType", 0, REG_SZ, (const BYTE*)st->licenseType, (DWORD)strlen(st->licenseType) + 1);
        RegSetValueExA(hKey, "Message", 0, REG_SZ, (const BYTE*)st->message, (DWORD)strlen(st->message) + 1);
        DWORD pluginId = (DWORD)st->pluginId;
        RegSetValueExA(hKey, "PluginId", 0, REG_DWORD, (const BYTE*)&pluginId, sizeof(DWORD));
        DWORD maxActivations = (DWORD)st->maxActivations;
        RegSetValueExA(hKey, "MaxActivations", 0, REG_DWORD, (const BYTE*)&maxActivations, sizeof(DWORD));
        DWORD currentActivations = (DWORD)st->currentActivations;
        RegSetValueExA(hKey, "CurrentActivations", 0, REG_DWORD, (const BYTE*)&currentActivations, sizeof(DWORD));
        DWORD maxSubLicenses = (DWORD)st->maxSubLicenses;
        RegSetValueExA(hKey, "MaxSubLicenses", 0, REG_DWORD, (const BYTE*)&maxSubLicenses, sizeof(DWORD));
        DWORD currentSubLicenses = (DWORD)st->currentSubLicenses;
        RegSetValueExA(hKey, "CurrentSubLicenses", 0, REG_DWORD, (const BYTE*)&currentSubLicenses, sizeof(DWORD));
        DWORD savePassword = (DWORD)st->savePassword;
        RegSetValueExA(hKey, "SavePassword", 0, REG_DWORD, (const BYTE*)&savePassword, sizeof(DWORD));
        DWORD isShared = (DWORD)st->isSharedLicense;
        RegSetValueExA(hKey, "IsSharedLicense", 0, REG_DWORD, (const BYTE*)&isShared, sizeof(DWORD));
        DWORD licensed = (DWORD)st->licensed;
        RegSetValueExA(hKey, "Licensed", 0, REG_DWORD, (const BYTE*)&licensed, sizeof(DWORD));
        if (st->isSharedLicense) {
            RegSetValueExA(hKey, "SharedOwnerName", 0, REG_SZ, (const BYTE*)st->sharedOwnerName, (DWORD)strlen(st->sharedOwnerName) + 1);
            RegSetValueExA(hKey, "SharedOwnerEmail", 0, REG_SZ, (const BYTE*)st->sharedOwnerEmail, (DWORD)strlen(st->sharedOwnerEmail) + 1);
            RegSetValueExA(hKey, "SharedDate", 0, REG_SZ, (const BYTE*)st->sharedDate, (DWORD)strlen(st->sharedDate) + 1);
        }
        RegCloseKey(hKey);
        TM_INFO("License: Saved metadata to registry (shared=%d)", st->isSharedLicense);
    }
}

void LoadFromRegistry(LicenseState* st)
{
    if (!st) return;

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLicenseRegKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0, size = 0;

        size = sizeof(st->licenseKey);
        if (RegQueryValueExA(hKey, "LicenseKey", nullptr, &type, (BYTE*)st->licenseKey, &size) != ERROR_SUCCESS)
            st->licenseKey[0] = '\0';

        DWORD statusVal = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "LicenseStatus", nullptr, &type, (BYTE*)&statusVal, &size) == ERROR_SUCCESS)
            st->status = (LicenseStatus)statusVal;

        size = sizeof(st->licExpiry);
        if (RegQueryValueExA(hKey, "LicenseExpiry", nullptr, &type, (BYTE*)st->licExpiry, &size) != ERROR_SUCCESS)
            st->licExpiry[0] = '\0';

        size = sizeof(st->tokenExpiry);
        if (RegQueryValueExA(hKey, "TokenExpiry", nullptr, &type, (BYTE*)st->tokenExpiry, &size) != ERROR_SUCCESS)
            st->tokenExpiry[0] = '\0';

        size = sizeof(st->licenseType);
        if (RegQueryValueExA(hKey, "LicenseType", nullptr, &type, (BYTE*)st->licenseType, &size) != ERROR_SUCCESS)
            st->licenseType[0] = '\0';

        size = sizeof(st->message);
        if (RegQueryValueExA(hKey, "Message", nullptr, &type, (BYTE*)st->message, &size) != ERROR_SUCCESS)
            st->message[0] = '\0';

        DWORD pluginId = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "PluginId", nullptr, &type, (BYTE*)&pluginId, &size) == ERROR_SUCCESS)
            st->pluginId = (int)pluginId;

        DWORD maxActivations = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "MaxActivations", nullptr, &type, (BYTE*)&maxActivations, &size) == ERROR_SUCCESS)
            st->maxActivations = (int)maxActivations;

        DWORD currentActivations = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "CurrentActivations", nullptr, &type, (BYTE*)&currentActivations, &size) == ERROR_SUCCESS)
            st->currentActivations = (int)currentActivations;

        DWORD maxSubLicenses = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "MaxSubLicenses", nullptr, &type, (BYTE*)&maxSubLicenses, &size) == ERROR_SUCCESS)
            st->maxSubLicenses = (int)maxSubLicenses;

        DWORD currentSubLicenses = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "CurrentSubLicenses", nullptr, &type, (BYTE*)&currentSubLicenses, &size) == ERROR_SUCCESS)
            st->currentSubLicenses = (int)currentSubLicenses;

        DWORD savePassword = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "SavePassword", nullptr, &type, (BYTE*)&savePassword, &size) == ERROR_SUCCESS)
            st->savePassword = (bool)savePassword;

        DWORD isShared = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "IsSharedLicense", nullptr, &type, (BYTE*)&isShared, &size) == ERROR_SUCCESS)
            st->isSharedLicense = (bool)isShared;

        if (st->isSharedLicense) {
            size = sizeof(st->sharedOwnerName);
            if (RegQueryValueExA(hKey, "SharedOwnerName", nullptr, &type, (BYTE*)st->sharedOwnerName, &size) != ERROR_SUCCESS)
                st->sharedOwnerName[0] = '\0';

            size = sizeof(st->sharedOwnerEmail);
            if (RegQueryValueExA(hKey, "SharedOwnerEmail", nullptr, &type, (BYTE*)st->sharedOwnerEmail, &size) != ERROR_SUCCESS)
                st->sharedOwnerEmail[0] = '\0';

            size = sizeof(st->sharedDate);
            if (RegQueryValueExA(hKey, "SharedDate", nullptr, &type, (BYTE*)st->sharedDate, &size) != ERROR_SUCCESS)
                st->sharedDate[0] = '\0';
        }

        DWORD licensed = 0;
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "Licensed", nullptr, &type, (BYTE*)&licensed, &size) == ERROR_SUCCESS)
            st->licensed = (bool)licensed;
        else
            st->licensed = (st->status == LIC_VALID);

        RegCloseKey(hKey);
        TM_INFO("License: Loaded metadata from registry — key=%s status=%d type=%s shared=%d", st->licenseKey, st->status, st->licenseType, st->isSharedLicense);
    }
}

bool Login(LicenseState* st, const wchar_t* email, const wchar_t* password)
{
    if (!st) return false;
    if (!email || !email[0] || !password || !password[0]) {
        strcpy_s(st->message, "Email and password are required");
        st->status = LIC_INVALID;
        st->licensed = false;
        return false;
    }

    char emailUtf8[256] = {}, passwordUtf8[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, email, -1, emailUtf8, sizeof(emailUtf8), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, password, -1, passwordUtf8, sizeof(passwordUtf8), nullptr, nullptr);

    char computerName[256] = {};
    DWORD nameSize = sizeof(computerName);
    GetComputerNameA(computerName, &nameSize);

    char jsonRequest[1024] = {};
    sprintf_s(jsonRequest, "{\"email\":\"%s\",\"password\":\"%s\",\"plugin\":\"%s\",\"device_name\":\"%s\"}",
              emailUtf8, passwordUtf8, kLicensePlugin, computerName[0] ? computerName : "UNKNOWN-DEVICE");

    char response[4096] = {};
    TM_INFO("License: Login request URL: https://djeventsuite.cloud/pages/api/login");
    TM_INFO("License: Login request JSON: %s", jsonRequest);
    TM_INFO("License: Logging in with email=%s", emailUtf8);

    if (!HttpRequest(L"https://djeventsuite.cloud/pages/api/login", "POST", jsonRequest, response, sizeof(response))) {
        strcpy_s(st->message, "Could not connect to license server");
        st->status = LIC_ERROR;
        TM_WARN("License: HTTP POST failed");
        return false;
    }

    TM_INFO("License: Server response: %s", response);

    char status[32] = {}, message[128] = {}, licenseKey[64] = {}, authToken[512] = {};
    char expiry[32] = {}, tokenExpiry[32] = {}, licenseType[32] = {};
    int pluginId = 0, maxActivations = 0, currentActivations = 0;

    TmJson::GetString(response, "status", status, sizeof(status));
    TmJson::GetString(response, "message", message, sizeof(message));
    TmJson::GetString(response, "license_key", licenseKey, sizeof(licenseKey));
    TmJson::GetString(response, "auth_token", authToken, sizeof(authToken));
    TmJson::GetString(response, "expires", expiry, sizeof(expiry));
    TmJson::GetString(response, "token_expires", tokenExpiry, sizeof(tokenExpiry));
    TmJson::GetString(response, "license_type", licenseType, sizeof(licenseType));
    TmJson::GetInt(response, "plugin_id", &pluginId);
    TmJson::GetInt(response, "max_activations", &maxActivations);
    TmJson::GetInt(response, "current_activations", &currentActivations);

    int maxSubLicenses = 0, currentSubLicenses = 0;
    TmJson::GetInt(response, "max_sub_licenses", &maxSubLicenses);
    TmJson::GetInt(response, "current_sub_licenses", &currentSubLicenses);

    char isSharedStr[16] = {};
    TmJson::GetString(response, "is_shared", isSharedStr, sizeof(isSharedStr));
    bool isShared = (strcmp(isSharedStr, "true") == 0 || strcmp(isSharedStr, "1") == 0 || strstr(response, "\"is_shared\":true") != nullptr);

    char sharedOwnerName[128] = {}, sharedOwnerEmail[128] = {}, sharedDate[24] = {};
    if (isShared) {
        const char* sharedInfoStart = strstr(response, "\"shared_info\":");
        if (sharedInfoStart) {
            TmJson::GetString(sharedInfoStart, "owner_name", sharedOwnerName, sizeof(sharedOwnerName));
            TmJson::GetString(sharedInfoStart, "owner_email", sharedOwnerEmail, sizeof(sharedOwnerEmail));
            TmJson::GetString(sharedInfoStart, "shared_date", sharedDate, sizeof(sharedDate));
        }
    }

    strcpy_s(st->message, message);
    if (strcmp(status, "active") == 0) {
        st->status = LIC_VALID;
        st->licensed = true;
        wcscpy_s(st->userEmail, email);
        strcpy_s(st->licenseKey, licenseKey);
        strcpy_s(st->authToken, authToken);
        strcpy_s(st->licExpiry, expiry);
        strcpy_s(st->tokenExpiry, tokenExpiry);
        strcpy_s(st->licenseType, licenseType);
        st->pluginId = pluginId;
        st->maxActivations = maxActivations;
        st->currentActivations = currentActivations;
        st->maxSubLicenses = maxSubLicenses;
        st->currentSubLicenses = currentSubLicenses;
        st->isSharedLicense = isShared;
        strcpy_s(st->sharedOwnerName, sharedOwnerName);
        strcpy_s(st->sharedOwnerEmail, sharedOwnerEmail);
        strcpy_s(st->sharedDate, sharedDate);

        SaveSecureCredentials(st);
        SaveToRegistry(st);

        if (isShared) {
            sprintf_s(st->message, "Login successful! (Shared by %s)", sharedOwnerName[0] ? sharedOwnerName : "license owner");
            TM_INFO("License: LOGIN SUCCESS (SHARED) — %s expires %s (%d/%d devices) — Shared by %s <%s> on %s",
                    licenseType, expiry, currentActivations, maxActivations, sharedOwnerName, sharedOwnerEmail, sharedDate);
        } else {
            strcpy_s(st->message, "Login successful!");
            TM_INFO("License: LOGIN SUCCESS — %s expires %s (%d/%d devices)",
                    licenseType, expiry, currentActivations, maxActivations);
        }

        ActivateDevice(st);
        return true;
    }

    if (strcmp(status, "expired") == 0) {
        st->status = LIC_EXPIRED;
        st->licensed = false;
        strcpy_s(st->licExpiry, expiry);
        if (st->message[0] == '\0') strcpy_s(st->message, "Your license has expired. Please renew to continue using TellyMedia.");
    } else if (strcmp(status, "revoked") == 0) {
        st->status = LIC_REVOKED;
        st->licensed = false;
        if (st->message[0] == '\0') strcpy_s(st->message, "Your license has been revoked. Please contact support.");
    } else if (strcmp(status, "error") == 0) {
        st->status = LIC_INVALID;
        st->licensed = false;
        if (st->message[0] == '\0') {
            if (strstr(message, "Invalid email") || strstr(message, "password")) {
                strcpy_s(st->message, "Invalid email or password. Please check your credentials.");
            } else if (strstr(message, "No active license")) {
                strcpy_s(st->message, "No active license found for TellyMedia. Please purchase a license.");
            } else if (strstr(message, "Plugin not found")) {
                strcpy_s(st->message, "Plugin not found on server. Please contact support.");
            } else {
                strcpy_s(st->message, "Login failed. Please check your credentials and try again.");
            }
        }
    } else {
        st->status = LIC_INVALID;
        st->licensed = false;
        if (st->message[0] == '\0') strcpy_s(st->message, "Login failed. Please check your credentials and try again.");
    }

    return false;
}

bool Validate(LicenseState* st)
{
    if (!st) return false;
    if (st->licenseKey[0] == '\0' || st->authToken[0] == '\0') {
        st->status = LIC_UNCHECKED;
        st->licensed = false;
        return false;
    }

    char jsonRequest[1024] = {};
    sprintf_s(jsonRequest, "{\"license_key\":\"%s\",\"auth_token\":\"%s\"}", st->licenseKey, st->authToken);

    char response[4096] = {};
    if (!HttpRequest(L"https://djeventsuite.cloud/pages/api/verify-license", "POST", jsonRequest, response, sizeof(response))) {
        TM_WARN("License: Validate HTTP failed — using cached status");
        return st->licensed;
    }

    char status[32] = {}, message[128] = {}, expiry[32] = {}, licenseType[32] = {};
    int maxActivations = 0, currentActivations = 0;
    TmJson::GetString(response, "status", status, sizeof(status));
    TmJson::GetString(response, "message", message, sizeof(message));
    TmJson::GetString(response, "expires", expiry, sizeof(expiry));
    TmJson::GetString(response, "license_type", licenseType, sizeof(licenseType));
    TmJson::GetInt(response, "max_activations", &maxActivations);
    TmJson::GetInt(response, "current_activations", &currentActivations);

    strcpy_s(st->message, message);
    if (strcmp(status, "active") == 0) {
        st->status = LIC_VALID;
        st->licensed = true;
        strcpy_s(st->licExpiry, expiry);
        strcpy_s(st->licenseType, licenseType);
        st->maxActivations = maxActivations;
        st->currentActivations = currentActivations;
        SaveToRegistry(st);
        TM_INFO("License: VALID — %s expires %s (%d/%d devices)", licenseType, expiry, currentActivations, maxActivations);
        return true;
    }

    if (strcmp(status, "expired") == 0) {
        st->status = LIC_EXPIRED;
        st->licensed = false;
        strcpy_s(st->licExpiry, expiry);
        SaveToRegistry(st);
    } else if (strcmp(status, "revoked") == 0) {
        st->status = LIC_REVOKED;
        st->licensed = false;
    } else if (strcmp(status, "token_expired") == 0) {
        st->status = LIC_INVALID;
        st->licensed = false;
        strcpy_s(st->message, "Session expired. Please login again.");
        TM_INFO("License: Session expired (14 days) - clearing credentials");
        st->authToken[0] = '\0';
        SaveToRegistry(st);
    } else {
        st->status = LIC_INVALID;
        st->licensed = false;
    }

    return false;
}

bool ActivateDevice(LicenseState* st)
{
    if (!st) return false;
    if (st->licenseKey[0] == '\0' || st->authToken[0] == '\0') {
        TM_WARN("License: Cannot activate device - no license key or auth token");
        return false;
    }

    char deviceId[256] = {};
    char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(computerName);
    if (GetComputerNameA(computerName, &size)) {
        DWORD volumeSerial = 0;
        GetVolumeInformationA("C:\\", nullptr, 0, &volumeSerial, nullptr, nullptr, nullptr, 0);
        sprintf_s(deviceId, "%s-%08X", computerName, volumeSerial);
    } else {
        strcpy_s(deviceId, "UNKNOWN-DEVICE");
        strcpy_s(computerName, "UNKNOWN-DEVICE");
    }

    char jsonRequest[1024] = {};
    sprintf_s(jsonRequest, "{\"license_key\":\"%s\",\"auth_token\":\"%s\",\"device_id\":\"%s\",\"device_name\":\"%s\"}",
              st->licenseKey, st->authToken, deviceId, computerName);

    char response[4096] = {};
    TM_INFO("License: Activating device %s", deviceId);
    if (!HttpRequest(L"https://djeventsuite.cloud/pages/api/activate-device", "POST", jsonRequest, response, sizeof(response))) {
        TM_WARN("License: Device activation HTTP failed");
        return false;
    }

    char status[32] = {}, message[128] = {};
    int currentActivations = 0, maxActivations = 0;
    TmJson::GetString(response, "status", status, sizeof(status));
    TmJson::GetString(response, "message", message, sizeof(message));
    TmJson::GetInt(response, "current_activations", &currentActivations);
    TmJson::GetInt(response, "max_activations", &maxActivations);

    if (strcmp(status, "active") == 0) {
        st->currentActivations = currentActivations;
        st->maxActivations = maxActivations;
        SaveToRegistry(st);
        TM_INFO("License: Device activated — %d/%d devices", currentActivations, maxActivations);
        return true;
    }

    TM_WARN("License: Device activation failed — %s", message);
    return false;
}

void Logout(LicenseState* st)
{
    if (!st) return;

    TM_INFO("License: Logging out and clearing credentials");
    st->userEmail[0] = L'\0';
    st->licenseKey[0] = '\0';
    st->authToken[0] = '\0';
    st->licExpiry[0] = '\0';
    st->tokenExpiry[0] = '\0';
    st->licenseType[0] = '\0';
    st->message[0] = '\0';
    st->sharedOwnerName[0] = '\0';
    st->sharedOwnerEmail[0] = '\0';
    st->sharedDate[0] = '\0';
    st->pluginId = 0;
    st->maxActivations = 0;
    st->currentActivations = 0;
    st->maxSubLicenses = 0;
    st->currentSubLicenses = 0;
    st->status = LIC_UNCHECKED;
    st->licensed = false;
    st->savePassword = false;
    st->isSharedLicense = false;

    CredentialDelete(kCredentialTarget);

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kLicenseRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"LicenseKey");
        RegDeleteValueW(hKey, L"LicenseStatus");
        RegDeleteValueW(hKey, L"LicenseExpiry");
        RegDeleteValueW(hKey, L"TokenExpiry");
        RegDeleteValueW(hKey, L"LicenseType");
        RegDeleteValueW(hKey, L"Message");
        RegDeleteValueW(hKey, L"PluginId");
        RegDeleteValueW(hKey, L"MaxActivations");
        RegDeleteValueW(hKey, L"CurrentActivations");
        RegDeleteValueW(hKey, L"MaxSubLicenses");
        RegDeleteValueW(hKey, L"CurrentSubLicenses");
        RegDeleteValueW(hKey, L"SavePassword");
        RegDeleteValueW(hKey, L"IsSharedLicense");
        RegDeleteValueW(hKey, L"SharedOwnerName");
        RegDeleteValueW(hKey, L"SharedOwnerEmail");
        RegDeleteValueW(hKey, L"SharedDate");
        RegDeleteValueW(hKey, L"Licensed");
        RegCloseKey(hKey);
    }
}

}
