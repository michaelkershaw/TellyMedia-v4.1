#pragma once
#include "tm/TmPlatform.h"
#include "tm/TmTypes.h"
#include "tm/TmUI.h"

#if defined(VDJ_MAC)

// ─── WKWebView-based UI host for macOS ────────────────────────────────────────
// Replaces the Win32/GDI+ TmUI.cpp. The C++ side owns all state (TmUIData)
// and serializes it to JSON for the JS frontend. JS sends commands back via
// the WKScriptMessageHandler bridge.

#include <string>
#include <functional>

// Opaque ObjC type forward declarations
#ifdef __OBJC__
@class TmWebViewDelegate;
@class WKWebView;
@class NSView;
typedef TmWebViewDelegate* TmWebViewDelegateRef;
typedef WKWebView* WKWebViewRef;
typedef NSView* NSViewRef;
#else
typedef void* TmWebViewDelegateRef;
typedef void* WKWebViewRef;
typedef void* NSViewRef;
#endif

struct TmWebViewData {
    // The TmUIData is shared with the C++ side
    TmUIData* uiData;

    // WKWebView and delegate
    WKWebViewRef webView;
    TmWebViewDelegateRef delegate;

    // The parent NSView
    NSViewRef parentView;

    // Callback to dispatch commands from JS
    std::function<void(const char* json)> onCommand;

    // Pending state JSON to send to JS
    std::string pendingStateJson;
    bool needsStatePush;
};

namespace TmWebView {
    // Create the WKWebView and embed it in the parent NSView.
    // The web/ folder must be at webPath (absolute path to directory containing index.html).
    TmWebViewData* Create(TmUIData* uiData, NSViewRef parentView, const char* webPath);
    void Destroy(TmWebViewData* data);

    // Push full state to JS as JSON. Called whenever C++ state changes.
    void PushState(TmWebViewData* data, const char* json);

    // Push a thumbnail to JS (slot-level update, avoids full state refresh).
    void PushThumbnail(TmWebViewData* data, int bank, int slot, const char* base64Data);

    // Push a panel image to JS.
    void PushPanelImage(TmWebViewData* data, int panelIndex, const char* base64Data);

    // Push an event to JS (e.g. now-playing text update).
    void PushEvent(TmWebViewData* data, const char* name, const char* jsonData);

    // Resize the webview to fill the parent view.
    void Resize(TmWebViewData* data, int width, int height);

    // Get the JSON state serialization of the TmUIData.
    // (Implemented in TmWebView.cpp — shared between platforms.)
    std::string SerializeState(TmUIData* d);

    // Dispatch a command received from JS. Mutates TmUIData and calls host callbacks.
    // (Implemented in TmWebView.cpp — shared between platforms.)
    void DispatchCommand(TmUIData* d, const char* json);
}

#endif // VDJ_MAC
