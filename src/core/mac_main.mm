// mac_main.mm — macOS bundle entry point for VirtualDJ plugin
// Replaces dllmain.cpp. VirtualDJ loads the .bundle and calls DllGetClassObject.

#if defined(VDJ_MAC)

#import <Cocoa/Cocoa.h>
#include "tm/TmPlugin.h"
#include "tm/TmLogger.h"
#include <objc/objc.h>
#include <objc/runtime.h>

// ─── Bundle load/unload ──────────────────────────────────────────────────────
__attribute__((constructor))
static void TmBundleLoad() {
    TmLogger::Init();
    TM_INFO("TellyMedia bundle loaded");
}

__attribute__((destructor))
static void TmBundleUnload() {
    TM_INFO("TellyMedia bundle unloading");
    TmLogger::Shutdown();
}

// ─── Class factory (same signature as Windows) ───────────────────────────────
// VirtualDJ calls this to get the plugin instance.
extern "C" VDJ_EXPORT HRESULT VDJ_API DllGetClassObject(const GUID& rclsid, const GUID& riid, void** ppObject) {
    if (rclsid == CLSID_VdjPlugin8 && riid == IID_IVdjPluginVideoFx8) {
        TmPlugin* p = new TmPlugin();
        *ppObject = static_cast<IVdjPluginVideoFx8*>(p);
        return S_OK;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

#endif // VDJ_MAC
