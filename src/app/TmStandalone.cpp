// Standalone Phase 1 harness: runs the TellyMedia v4 UI without VirtualDJ so the
// banks/grid/sidebar can be developed and tested independently.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "tm/TmUI.h"
#include "tm/TmLogger.h"

// Enable per-monitor DPI awareness if available (best resize fidelity).
static void EnableDpiAwareness()
{
    typedef BOOL (WINAPI *PFN_SetCtx)(DPI_AWARENESS_CONTEXT);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        PFN_SetCtx p = (PFN_SetCtx)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (p && p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    EnableDpiAwareness();
    TmLogger::Init();
    TM_INFO("Standalone Phase 1 harness starting");

    HWND hwnd = TmUI::Create(hInst, nullptr, nullptr);
    if (!hwnd) { TM_ERR("Failed to create UI window"); return 1; }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    TmUI::Destroy(hwnd);
    TmLogger::Shutdown();
    return (int)msg.wParam;
}
