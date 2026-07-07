// DLL entry point + VirtualDJ class factory for the TellyMedia v4 plugin.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "tm/TmPlugin.h"
#include "tm/TmLogger.h"

static HINSTANCE g_hInstance = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
        TmLogger::Init();
        TM_INFO("DLL_PROCESS_ATTACH");
        break;
    case DLL_PROCESS_DETACH:
        TM_INFO("DLL_PROCESS_DETACH");
        TmLogger::Shutdown();
        break;
    }
    return TRUE;
}

// VirtualDJ calls this directly expecting the plugin object.
extern "C" HRESULT VDJ_API DllGetClassObject(const GUID& rclsid, const GUID& riid, void** ppObject)
{
    if (rclsid == CLSID_VdjPlugin8 && riid == IID_IVdjPluginVideoFx8) {
        TmPlugin* p = new TmPlugin();
        *ppObject = static_cast<IVdjPluginVideoFx8*>(p);
        return S_OK;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
