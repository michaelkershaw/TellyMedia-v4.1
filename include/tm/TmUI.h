#pragma once
#include "tm/TmPlatform.h"
#include "tm/TmTypes.h"
#include "tm/TmTheme.h"
#include "vdj/vdjPlugin8.h"

#if defined(VDJ_WIN)
namespace Gdiplus { class Image; }
#elif defined(VDJ_MAC)
// CGImageRef is defined by CoreGraphics on macOS.
// When compiling ObjC++ with Cocoa, it's already available.
// For pure C++ compilation, provide a fallback typedef.
#ifndef __OBJC__
typedef void* CGImageRef;
#endif
#endif

// Optional host callback so the UI can drive the plugin/media engine in later
// phases. In Phase 1 (standalone) this is null and the UI just manages data.
struct ITmUIHost {
    virtual ~ITmUIHost() {}
    // Load + play a media file (image or video) on the master output.
    virtual void HostPlayMedia(const wchar_t* path, int scaleMode) {}
    virtual void HostStop() {}
    virtual void HostPause() {}
    virtual void HostScaleMode(int mode) {}
    virtual void HostSlideshowStart() {}
    virtual void HostSlideshowStop() {}
    virtual void HostTransitionMode(int mode) {}
    virtual void HostRenderMode(int mode) {}
    // Retrieve the current now-playing text for song panels / previews.
    virtual void HostGetNowPlayingText(wchar_t* outText, int outTextMax) {
        if (outText && outTextMax > 0) outText[0] = L'\0';
    }
    virtual void HostGetRenderModeText(wchar_t* outText, int outTextMax) {
        if (outText && outTextMax > 0) outText[0] = L'\0';
    }
    // Update overlay panels for video output
    virtual void UISetOverlayPanels(const OverlayPanel* panels, int count) {}
    // Shader control callbacks
    virtual void HostSetShadersEnabled(bool enabled) {}
    virtual void HostSetShaderKaraokeDisable(bool disable) {}
    virtual void HostSetKaraokeAutoHide(bool enabled) {}
    virtual void HostSetMainShader(int index) {}
    virtual void HostReloadShaders() {}
    virtual void HostGetShaderNames(wchar_t* outNames, int outNamesMax) {
        if (outNames && outNamesMax > 0) outNames[0] = L'\0';
    }
    virtual bool HostGetLicenseState(LicenseState* outState) { return false; }
    virtual bool HostLicenseLogin(const wchar_t* email, const wchar_t* password, bool savePassword) { return false; }
    virtual bool HostLicenseValidate() { return false; }
    virtual bool HostLicenseActivate() { return false; }
    virtual void HostLicenseLogout() {}
};

enum TmRenderMode {
    TM_RENDER_AUTO = 0,
    TM_RENDER_CPU  = 1,
    TM_RENDER_GPU  = 2
};

// Sidebar / slideshow settings (Phase 1 owns these; engine reads them later).
struct SidebarSettings {
    bool playing;
    int  scaleMode;        // SCALE_ASPECT / STRETCH / NOSCALE
    bool ssEnabled;       // If false, slideshow is disabled (allows overlays/music videos only)
    bool ssRunning;
    int  ssDelaySec;       // 1..60
    int  ssTransition;     // 0=Cut 1=Fade 2=Crossfade
    int  ssDirection;      // 0=Fwd 1=Back 2=Random
    bool ssLoop;
    int  renderMode;       // TM_RENDER_AUTO / TM_RENDER_CPU / TM_RENDER_GPU

    // Shader settings
    bool shadersEnabled;
    bool shaderDisableOnKaraoke;
    int  mainShaderIndex;
};

// Tabs
enum TmTab { TAB_MEDIA = 0, TAB_LAYOUT = 1, TAB_OPTIONS = 2, TAB_LICENSE = 3, TAB_COUNT = 4 };

// All UI state attached to the main window.
struct TmUIData {
    VDJ_WINDOW      hWnd;
    VDJ_HINSTANCE   hInst;
    ITmUIHost*      host;

    LayoutMetrics   metrics;
    TmFonts         fonts;
    float           dpi;

    int             curTab;
    GridState       grid;
    SidebarSettings sidebar;
    bool            karaokeAutoHide;

    // Interaction / scrolling
    int             scrollY;
    int             contentHeight;
    int             hoverSlot;
    int             hoverBank;
    int             hoverTab;
    int             hoverSidebar;
    bool            tracking;
    bool            slotClickPending;
    int             slotClickBank;
    int             slotClickSlot;
    POINT           slotClickStart;
    int             slotMoveSourceBank;
    bool            slotMovePending;
    int             slotMoveSource;

    // Scrollbar drag state
    bool            scrollDragging;
    int             scrollDragArea;
    int             scrollDragOffset;

    // Cached hit rectangles (recomputed each layout)
    RECT            rcTabs[TAB_COUNT];
    RECT            rcBanks[NUM_BANKS];
    RECT            rcGrid;
    RECT            rcSidebar;
    RECT            rcCloseBtn;
    bool            hoverCloseBtn;

    // ── License tab layout ──
    RECT            rcLicenseCard;
    RECT            rcLicenseBadge;
    RECT            rcLicenseDetails;
    RECT            rcLicenseStatus;
    RECT            rcLicenseLogo;
    RECT            rcLicenseButtons[4];
    RECT            rcLicenseEmail;
    RECT            rcLicensePassword;
    RECT            rcLicenseSave;
#if defined(VDJ_WIN)
    HWND            editLicenseEmail;
    HWND            editLicensePassword;
    HWND            chkLicenseSave;
#elif defined(VDJ_MAC)
    void*           editLicenseEmail;    // NSTextField*
    void*           editLicensePassword; // NSSecureTextField*
    void*           chkLicenseSave;      // NSButton*
#endif

    // ── Layout tab (overlay panel editor) ──
    OverlayPanel    layoutPanels[MAX_OVERLAY_PANELS];
    int             numLayoutPanels;
    int             selectedPanel;
    int             hoverPanel;
    RECT            rcCanvas;
    int             dragPanel;
    int             dragMode;
    POINT           dragStart;
    float           dragOrigX, dragOrigY, dragOrigW, dragOrigH;
    int             panelListScrollY;
    int             sidebarScrollY;
    int             sidebarContentHeight;
    RECT            rcPanelList;
    bool            directSelectEnabled;

    // Per-panel image cache for the canvas preview.
#if defined(VDJ_WIN)
    Gdiplus::Image* panelImageCache[MAX_OVERLAY_PANELS];
#elif defined(VDJ_MAC)
    CGImageRef      panelImageCache[MAX_OVERLAY_PANELS];
#endif
};

namespace TmUI {
    // Register class + create the main window. parent may be null (standalone).
    VDJ_WINDOW Create(VDJ_HINSTANCE hInst, ITmUIHost* host, VDJ_WINDOW parent);
    // Create as an embeddable child window for VirtualDJ's settings dialog.
    VDJ_WINDOW CreateEmbedded(VDJ_HINSTANCE hInst, ITmUIHost* host);
    void Destroy(VDJ_WINDOW hWnd);

    void SaveState(TmUIData* d);
    void LoadState(TmUIData* d);
}
