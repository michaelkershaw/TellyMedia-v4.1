// TmServicesMac.mm — macOS services: HTTP (URLSession), JSON parsing,
// and licensing (Keychain + NSUserDefaults).
// Replaces the WinHTTP/WinCred/Registry-based TmServices.cpp.

#if defined(VDJ_MAC)

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include "tm/TmServices.h"
#include "tm/TmLogger.h"
#include <string>
#include <cstring>
#include <cstdlib>
#include <IOKit/IOKitLib.h>

namespace {
constexpr const char* kCredentialService = "com.tellymedia.reborn.authToken";
constexpr const char* kDefaultsKey = "com.tellymedia.reborn.license";
constexpr const char* kLicensePlugin = "tellymedia-reborn";

// ─── Synchronous HTTP request using NSURLSession ─────────────────────────────
static bool HttpRequest(const wchar_t* url, const char* method, const char* body,
                        char* response, int maxResponse) {
    if (!url || !method || !response || maxResponse <= 0) return false;
    response[0] = '\0';

    // Convert wchar_t URL to NSString
    char urlUtf8[1024];
    wcstombs(urlUtf8, url, sizeof(urlUtf8));
    NSString* urlStr = [NSString stringWithUTF8String:urlUtf8];
    NSURL* nsUrl = [NSURL URLWithString:urlStr];
    if (!nsUrl) return false;

    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsUrl
                                                        timeoutInterval:30.0];
    [req setHTTPMethod:[NSString stringWithUTF8String:method]];
    if (body && body[0]) {
        [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        [req setHTTPBody:[NSData dataWithBytes:body length:strlen(body)]];
    }

    __block NSData* respData = nil;
    __block NSError* reqError = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:req
            completionHandler:^(NSData* data, NSURLResponse* resp, NSError* error) {
        respData = data;
        reqError = error;
        dispatch_semaphore_signal(sem);
    }];
    [task resume];

    // Wait up to 30 seconds
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));

    if (reqError) {
        TM_WARN("HTTP: Request error: %s", [[reqError localizedDescription] UTF8String]);
        return false;
    }
    if (!respData || [respData length] == 0) return false;

    int len = (int)[respData length];
    if (len >= maxResponse) len = maxResponse - 1;
    [respData getBytes:response length:len];
    response[len] = '\0';
    return len > 0;
}

static void ClearLicenseState(LicenseState* st) {
    if (!st) return;
    ZeroMemory(st, sizeof(*st));
    st->status = LIC_UNCHECKED;
}

// ─── Keychain helpers ────────────────────────────────────────────────────────
static bool KeychainStore(const char* account, const char* data) {
    if (!account || !data) return false;

    NSDictionary* query = @{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: [NSString stringWithUTF8String:kCredentialService],
        (__bridge id)kSecAttrAccount: [NSString stringWithUTF8String:account],
    };

    // Delete existing item first
    SecItemDelete((__bridge CFDictionaryRef)query);

    NSData* valueData = [NSData dataWithBytes:data length:strlen(data) + 1];
    NSMutableDictionary* addQuery = [query mutableCopy];
    [addQuery setObject:valueData forKey:(__bridge id)kSecValueData];
    [addQuery setObject:(__bridge id)kSecAttrAccessibleWhenUnlocked forKey:(__bridge id)kSecAttrAccessible];

    OSStatus status = SecItemAdd((__bridge CFDictionaryRef)addQuery, nullptr);
    if (status != errSecSuccess) {
        TM_WARN("Keychain: SecItemAdd failed: %d", (int)status);
        return false;
    }
    return true;
}

static bool KeychainRead(const char* account, char* out, int maxOut) {
    if (!account || !out || maxOut <= 0) return false;
    out[0] = '\0';

    NSDictionary* query = @{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: [NSString stringWithUTF8String:kCredentialService],
        (__bridge id)kSecAttrAccount: [NSString stringWithUTF8String:account],
        (__bridge id)kSecReturnData: @YES,
        (__bridge id)kSecMatchLimit: (__bridge id)kSecMatchLimitOne,
    };

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status != errSecSuccess || !result) {
        return false;
    }

    NSData* data = (__bridge_transfer NSData*)result;
    if ([data length] > 0 && [data length] < (NSUInteger)maxOut) {
        memcpy(out, [data bytes], [data length]);
        out[[data length] - 1] = '\0'; // Strip null terminator from stored data
        return true;
    }
    return false;
}

static bool KeychainDelete(const char* account) {
    if (!account) return false;
    NSDictionary* query = @{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: [NSString stringWithUTF8String:kCredentialService],
        (__bridge id)kSecAttrAccount: [NSString stringWithUTF8String:account],
    };
    OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
    return status == errSecSuccess || status == errSecItemNotFound;
}

// ─── NSUserDefaults helpers (replaces Registry) ──────────────────────────────
static NSString* DefaultsKey(const char* field) {
    return [NSString stringWithFormat:@"%@.%s",
        [NSString stringWithUTF8String:kDefaultsKey], field];
}

static void DefaultsSetString(const char* field, const char* value) {
    [[NSUserDefaults standardUserDefaults]
        setObject:[NSString stringWithUTF8String:value ?: ""]
           forKey:DefaultsKey(field)];
}

static void DefaultsSetInt(const char* field, int value) {
    [[NSUserDefaults standardUserDefaults]
        setInteger:value forKey:DefaultsKey(field)];
}

static void DefaultsSetBool(const char* field, bool value) {
    [[NSUserDefaults standardUserDefaults]
        setBool:value forKey:DefaultsKey(field)];
}

static std::string DefaultsGetString(const char* field) {
    NSString* s = [[NSUserDefaults standardUserDefaults]
        stringForKey:DefaultsKey(field)];
    return s ? std::string([s UTF8String]) : "";
}

static int DefaultsGetInt(const char* field, int defVal = 0) {
    NSNumber* n = [[NSUserDefaults standardUserDefaults]
        objectForKey:DefaultsKey(field)];
    return n ? [n intValue] : defVal;
}

static bool DefaultsGetBool(const char* field, bool defVal = false) {
    NSNumber* n = [[NSUserDefaults standardUserDefaults]
        objectForKey:DefaultsKey(field)];
    return n ? [n boolValue] : defVal;
}

} // anonymous namespace

// ─── TmHttp namespace ────────────────────────────────────────────────────────
namespace TmHttp {

bool PostJson(const wchar_t* host, uint16_t port, bool https,
              const wchar_t* path, const char* jsonBody,
              char* response, int maxResponse) {
    if (!host || !path || !response || maxResponse <= 0) return false;

    wchar_t url[1024];
    swprintf(url, sizeof(url)/sizeof(url[0]), L"%s://%s:%u%S",
             https ? L"https" : L"http", host, (unsigned)port, path);
    return HttpRequest(url, "POST", jsonBody, response, maxResponse);
}

bool GetText(const wchar_t* host, uint16_t port, bool https,
             const wchar_t* path, char* response, int maxResponse) {
    if (!host || !path || !response || maxResponse <= 0) return false;

    wchar_t url[1024];
    swprintf(url, sizeof(url)/sizeof(url[0]), L"%s://%s:%u%S",
             https ? L"https" : L"http", host, (unsigned)port, path);
    return HttpRequest(url, "GET", nullptr, response, maxResponse);
}

} // namespace TmHttp

// ─── TmJson namespace (platform-agnostic, same as Windows) ───────────────────
namespace TmJson {

bool GetString(const char* json, const char* key, char* out, int maxOut) {
    if (!json || !key || !out || maxOut <= 0) return false;
    out[0] = '\0';

    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
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

bool GetInt(const char* json, const char* key, int* out) {
    if (!json || !key || !out) return false;
    char buf[64];
    if (!GetString(json, key, buf, sizeof(buf))) return false;
    *out = atoi(buf);
    return true;
}

} // namespace TmJson

// ─── TmLicense namespace ─────────────────────────────────────────────────────
namespace TmLicense {

void Init(LicenseState* st) {
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

void GetMachineId(char* out, int maxOut) {
    if (!out || maxOut <= 0) return;
    out[0] = '\0';

    // Get hardware UUID via IOKit
    io_service_t platformExpert = IOServiceGetMatchingService(
        kIOMasterPortDefault,
        IOServiceMatching("IOPlatformExpertDevice"));

    if (platformExpert) {
        CFTypeRef serialProp = IORegistryEntryCreateCFProperty(
            platformExpert, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
        if (serialProp) {
            char uuid[128] = {};
            CFStringGetCString((CFStringRef)serialProp, uuid, sizeof(uuid), kCFStringEncodingUTF8);
            // Use a hash of the UUID as device ID
            unsigned hash = 5381;
            for (const char* p = uuid; *p; p++) hash = ((hash << 5) + hash) + *p;
            snprintf(out, maxOut, "MAC-%08X", hash);
            CFRelease(serialProp);
        }
        IOObjectRelease(platformExpert);
    }

    if (out[0] == '\0') {
        // Fallback: hostname
        char hostname[256] = {};
        gethostname(hostname, sizeof(hostname));
        snprintf(out, maxOut, "MAC-%s", hostname);
    }
}

void SaveSecureCredentials(const LicenseState* st) {
    if (!st) return;

    if (st->savePassword && st->userEmail[0] != L'\0' && st->authToken[0] != '\0') {
        char emailUtf8[256];
        wcstombs(emailUtf8, st->userEmail, sizeof(emailUtf8));
        if (KeychainStore(emailUtf8, st->authToken)) {
            TM_INFO("License: Saved credentials to macOS Keychain");
        } else {
            TM_ERROR("License: Failed to save credentials to Keychain");
        }
    } else if (!st->savePassword) {
        char emailUtf8[256] = {};
        wcstombs(emailUtf8, st->userEmail, sizeof(emailUtf8));
        if (emailUtf8[0]) KeychainDelete(emailUtf8);
        TM_INFO("License: Removed credentials from Keychain (user opted not to save)");
    }
}

void LoadSecureCredentials(LicenseState* st) {
    if (!st) return;

    // Try to find stored credentials — we need to try with the stored email
    // First load email from defaults
    std::string email = DefaultsGetString("userEmail");
    if (email.empty()) {
        TM_INFO("License: No saved email found in defaults");
        return;
    }

    char authToken[512] = {};
    if (KeychainRead(email.c_str(), authToken, sizeof(authToken))) {
        mbstowcs(st->userEmail, email.c_str(), 255);
        strncpy(st->authToken, authToken, sizeof(st->authToken) - 1);
        st->savePassword = true;
        TM_INFO("License: Loaded credentials from Keychain — email=%s", email.c_str());
    } else {
        TM_INFO("License: No credentials found in Keychain");
    }
}

void SaveToRegistry(const LicenseState* st) {
    if (!st) return;

    DefaultsSetString("licenseKey", st->licenseKey);
    DefaultsSetInt("licenseStatus", (int)st->status);
    DefaultsSetString("licExpiry", st->licExpiry);
    DefaultsSetString("tokenExpiry", st->tokenExpiry);
    DefaultsSetString("licenseType", st->licenseType);
    DefaultsSetString("message", st->message);
    DefaultsSetInt("pluginId", st->pluginId);
    DefaultsSetInt("maxActivations", st->maxActivations);
    DefaultsSetInt("currentActivations", st->currentActivations);
    DefaultsSetInt("maxSubLicenses", st->maxSubLicenses);
    DefaultsSetInt("currentSubLicenses", st->currentSubLicenses);
    DefaultsSetBool("savePassword", st->savePassword);
    DefaultsSetBool("isSharedLicense", st->isSharedLicense);
    DefaultsSetBool("licensed", st->licensed);

    if (st->isSharedLicense) {
        DefaultsSetString("sharedOwnerName", st->sharedOwnerName);
        DefaultsSetString("sharedOwnerEmail", st->sharedOwnerEmail);
        DefaultsSetString("sharedDate", st->sharedDate);
    }

    // Also save email for Keychain lookup
    char emailUtf8[256];
    wcstombs(emailUtf8, st->userEmail, sizeof(emailUtf8));
    DefaultsSetString("userEmail", emailUtf8);

    [[NSUserDefaults standardUserDefaults] synchronize];
    TM_INFO("License: Saved metadata to NSUserDefaults (shared=%d)", st->isSharedLicense);
}

void LoadFromRegistry(LicenseState* st) {
    if (!st) return;

    std::string s;
    int i;
    bool b;

    s = DefaultsGetString("licenseKey"); strncpy(st->licenseKey, s.c_str(), sizeof(st->licenseKey) - 1);
    i = DefaultsGetInt("licenseStatus", (int)LIC_UNCHECKED); st->status = (LicenseStatus)i;
    s = DefaultsGetString("licExpiry"); strncpy(st->licExpiry, s.c_str(), sizeof(st->licExpiry) - 1);
    s = DefaultsGetString("tokenExpiry"); strncpy(st->tokenExpiry, s.c_str(), sizeof(st->tokenExpiry) - 1);
    s = DefaultsGetString("licenseType"); strncpy(st->licenseType, s.c_str(), sizeof(st->licenseType) - 1);
    s = DefaultsGetString("message"); strncpy(st->message, s.c_str(), sizeof(st->message) - 1);
    st->pluginId = DefaultsGetInt("pluginId");
    st->maxActivations = DefaultsGetInt("maxActivations");
    st->currentActivations = DefaultsGetInt("currentActivations");
    st->maxSubLicenses = DefaultsGetInt("maxSubLicenses");
    st->currentSubLicenses = DefaultsGetInt("currentSubLicenses");
    st->savePassword = DefaultsGetBool("savePassword");
    st->isSharedLicense = DefaultsGetBool("isSharedLicense");

    if (st->isSharedLicense) {
        s = DefaultsGetString("sharedOwnerName"); strncpy(st->sharedOwnerName, s.c_str(), sizeof(st->sharedOwnerName) - 1);
        s = DefaultsGetString("sharedOwnerEmail"); strncpy(st->sharedOwnerEmail, s.c_str(), sizeof(st->sharedOwnerEmail) - 1);
        s = DefaultsGetString("sharedDate"); strncpy(st->sharedDate, s.c_str(), sizeof(st->sharedDate) - 1);
    }

    b = DefaultsGetBool("licensed", false);
    st->licensed = b ? true : (st->status == LIC_VALID);

    // Load email
    s = DefaultsGetString("userEmail");
    if (!s.empty()) mbstowcs(st->userEmail, s.c_str(), 255);

    TM_INFO("License: Loaded metadata from NSUserDefaults — key=%s status=%d type=%s shared=%d",
            st->licenseKey, (int)st->status, st->licenseType, st->isSharedLicense);
}

bool Login(LicenseState* st, const wchar_t* email, const wchar_t* password) {
    if (!st) return false;
    if (!email || !email[0] || !password || !password[0]) {
        strcpy(st->message, "Email and password are required");
        st->status = LIC_INVALID;
        st->licensed = false;
        return false;
    }

    char emailUtf8[256], passwordUtf8[256];
    wcstombs(emailUtf8, email, sizeof(emailUtf8));
    wcstombs(passwordUtf8, password, sizeof(passwordUtf8));

    char deviceName[256] = {};
    gethostname(deviceName, sizeof(deviceName));

    char jsonRequest[1024];
    snprintf(jsonRequest, sizeof(jsonRequest),
             "{\"email\":\"%s\",\"password\":\"%s\",\"plugin\":\"%s\",\"device_name\":\"%s\"}",
             emailUtf8, passwordUtf8, kLicensePlugin, deviceName[0] ? deviceName : "UNKNOWN-DEVICE");

    char response[4096] = {};
    TM_INFO("License: Login request for email=%s", emailUtf8);

    if (!HttpRequest(L"https://djeventsuite.cloud/pages/api/login", "POST", jsonRequest, response, sizeof(response))) {
        strcpy(st->message, "Could not connect to license server");
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
    bool isShared = (strcmp(isSharedStr, "true") == 0 || strcmp(isSharedStr, "1") == 0 ||
                     strstr(response, "\"is_shared\":true") != nullptr);

    char sharedOwnerName[128] = {}, sharedOwnerEmail[128] = {}, sharedDate[24] = {};
    if (isShared) {
        const char* sharedInfoStart = strstr(response, "\"shared_info\":");
        if (sharedInfoStart) {
            TmJson::GetString(sharedInfoStart, "owner_name", sharedOwnerName, sizeof(sharedOwnerName));
            TmJson::GetString(sharedInfoStart, "owner_email", sharedOwnerEmail, sizeof(sharedOwnerEmail));
            TmJson::GetString(sharedInfoStart, "shared_date", sharedDate, sizeof(sharedDate));
        }
    }

    strcpy(st->message, message);
    if (strcmp(status, "active") == 0) {
        st->status = LIC_VALID;
        st->licensed = true;
        wcscpy(st->userEmail, email);
        strcpy(st->licenseKey, licenseKey);
        strcpy(st->authToken, authToken);
        strcpy(st->licExpiry, expiry);
        strcpy(st->tokenExpiry, tokenExpiry);
        strcpy(st->licenseType, licenseType);
        st->pluginId = pluginId;
        st->maxActivations = maxActivations;
        st->currentActivations = currentActivations;
        st->maxSubLicenses = maxSubLicenses;
        st->currentSubLicenses = currentSubLicenses;
        st->isSharedLicense = isShared;
        strcpy(st->sharedOwnerName, sharedOwnerName);
        strcpy(st->sharedOwnerEmail, sharedOwnerEmail);
        strcpy(st->sharedDate, sharedDate);

        SaveSecureCredentials(st);
        SaveToRegistry(st);

        if (isShared) {
            snprintf(st->message, sizeof(st->message),
                     "Login successful! (Shared by %s)", sharedOwnerName[0] ? sharedOwnerName : "license owner");
            TM_INFO("License: LOGIN SUCCESS (SHARED) — %s expires %s (%d/%d devices) — Shared by %s <%s> on %s",
                    licenseType, expiry, currentActivations, maxActivations,
                    sharedOwnerName, sharedOwnerEmail, sharedDate);
        } else {
            strcpy(st->message, "Login successful!");
            TM_INFO("License: LOGIN SUCCESS — %s expires %s (%d/%d devices)",
                    licenseType, expiry, currentActivations, maxActivations);
        }

        ActivateDevice(st);
        return true;
    }

    if (strcmp(status, "expired") == 0) {
        st->status = LIC_EXPIRED;
        st->licensed = false;
        strcpy(st->licExpiry, expiry);
        if (st->message[0] == '\0') strcpy(st->message, "Your license has expired. Please renew to continue using TellyMedia.");
    } else if (strcmp(status, "revoked") == 0) {
        st->status = LIC_REVOKED;
        st->licensed = false;
        if (st->message[0] == '\0') strcpy(st->message, "Your license has been revoked. Please contact support.");
    } else if (strcmp(status, "error") == 0) {
        st->status = LIC_INVALID;
        st->licensed = false;
        if (st->message[0] == '\0') {
            if (strstr(message, "Invalid email") || strstr(message, "password")) {
                strcpy(st->message, "Invalid email or password. Please check your credentials.");
            } else if (strstr(message, "No active license")) {
                strcpy(st->message, "No active license found for TellyMedia. Please purchase a license.");
            } else if (strstr(message, "Plugin not found")) {
                strcpy(st->message, "Plugin not found on server. Please contact support.");
            } else {
                strcpy(st->message, "Login failed. Please check your credentials and try again.");
            }
        }
    } else {
        st->status = LIC_INVALID;
        st->licensed = false;
        if (st->message[0] == '\0') strcpy(st->message, "Login failed. Please check your credentials and try again.");
    }

    return false;
}

bool Validate(LicenseState* st) {
    if (!st) return false;
    if (st->licenseKey[0] == '\0' || st->authToken[0] == '\0') {
        st->status = LIC_UNCHECKED;
        st->licensed = false;
        return false;
    }

    char jsonRequest[1024];
    snprintf(jsonRequest, sizeof(jsonRequest),
             "{\"license_key\":\"%s\",\"auth_token\":\"%s\"}", st->licenseKey, st->authToken);

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

    strcpy(st->message, message);
    if (strcmp(status, "active") == 0) {
        st->status = LIC_VALID;
        st->licensed = true;
        strcpy(st->licExpiry, expiry);
        strcpy(st->licenseType, licenseType);
        st->maxActivations = maxActivations;
        st->currentActivations = currentActivations;
        SaveToRegistry(st);
        TM_INFO("License: VALID — %s expires %s (%d/%d devices)", licenseType, expiry, currentActivations, maxActivations);
        return true;
    }

    if (strcmp(status, "expired") == 0) {
        st->status = LIC_EXPIRED;
        st->licensed = false;
        strcpy(st->licExpiry, expiry);
        SaveToRegistry(st);
    } else if (strcmp(status, "revoked") == 0) {
        st->status = LIC_REVOKED;
        st->licensed = false;
    } else if (strcmp(status, "token_expired") == 0) {
        st->status = LIC_INVALID;
        st->licensed = false;
        strcpy(st->message, "Session expired. Please login again.");
        TM_INFO("License: Session expired - clearing credentials");
        st->authToken[0] = '\0';
        SaveToRegistry(st);
    } else {
        st->status = LIC_INVALID;
        st->licensed = false;
    }

    return false;
}

bool ActivateDevice(LicenseState* st) {
    if (!st) return false;
    if (st->licenseKey[0] == '\0' || st->authToken[0] == '\0') {
        TM_WARN("License: Cannot activate device - no license key or auth token");
        return false;
    }

    char deviceId[256] = {};
    char deviceName[256] = {};
    GetMachineId(deviceId, sizeof(deviceId));
    gethostname(deviceName, sizeof(deviceName));

    char jsonRequest[1024];
    snprintf(jsonRequest, sizeof(jsonRequest),
             "{\"license_key\":\"%s\",\"auth_token\":\"%s\",\"device_id\":\"%s\",\"device_name\":\"%s\"}",
             st->licenseKey, st->authToken, deviceId, deviceName);

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

void Logout(LicenseState* st) {
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

    // Delete from Keychain
    std::string email = DefaultsGetString("userEmail");
    if (!email.empty()) KeychainDelete(email.c_str());

    // Clear NSUserDefaults
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary* allKeys = [defaults dictionaryRepresentation];
    NSString* prefix = [NSString stringWithUTF8String:kDefaultsKey];
    for (NSString* key in allKeys) {
        if ([key hasPrefix:prefix]) {
            [defaults removeObjectForKey:key];
        }
    }
    [defaults synchronize];
}

} // namespace TmLicense

#endif // VDJ_MAC
