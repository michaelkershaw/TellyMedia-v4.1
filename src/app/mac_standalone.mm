// mac_standalone.mm — macOS standalone test app for TellyMedia UI
// Creates a window with an embedded WKWebView for testing without VirtualDJ.

#if defined(VDJ_MAC)

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "tm/TmUI.h"
#include "tm/TmWebView.h"
#include "tm/TmLogger.h"
#include <string>

// ─── Stub host (no media engine in standalone) ───────────────────────────────
class TmStubHost : public ITmUIHost {
public:
    void HostPlayMedia(const wchar_t* path, int scaleMode) override {
        char buf[512];
        wcstombs(buf, path, sizeof(buf));
        TM_INFO("StubHost: play %s (scale=%d)", buf, scaleMode);
    }
    void HostStop() override { TM_INFO("StubHost: stop"); }
    void HostPause() override { TM_INFO("StubHost: pause"); }
};

// ─── App delegate ────────────────────────────────────────────────────────────
@interface TmAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSView* contentView;
@property (nonatomic, assign) TmUIData* uiData;
@property (nonatomic, assign) TmStubHost* host;
@property (nonatomic, assign) TmWebViewData* webViewData;
@end

@implementation TmAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // Init logger
    TmLogger::Init();
    TM_INFO("TellyMedia standalone starting");

    // Create host
    _host = new TmStubHost();

    // Create UI data
    _uiData = new TmUIData();
    memset(_uiData, 0, sizeof(TmUIData));
    _uiData->host = _host;
    _uiData->curTab = 0;
    _uiData->grid.curBank = 0;
    _uiData->grid.selectedSlot = -1;
    _uiData->sidebar.ssDelaySec = 5;
    _uiData->sidebar.ssLoop = true;
    _uiData->sidebar.scaleMode = SCALE_ASPECT;
    _uiData->sidebar.renderMode = TM_RENDER_AUTO;
    _uiData->numLayoutPanels = 0;
    _uiData->selectedPanel = -1;
    _uiData->directSelectEnabled = false;
    _uiData->karaokeAutoHide = true;

    // Init bank names
    const wchar_t* defaultNames[NUM_BANKS] = {
        L"Bank 1", L"Bank 2", L"Bank 3", L"Bank 4",
        L"Bank 5", L"Bank 6", L"Bank 7", L"Bank 8"
    };
    for (int i = 0; i < NUM_BANKS; i++) {
        wcscpy(_uiData->grid.bankNames[i], defaultNames[i]);
        _uiData->grid.bankColors[i] = RGB(0, 120, 215);
    }

    // Load saved state
    TmUI::LoadState(_uiData);

    // Create window
    NSRect frame = NSMakeRect(100, 100, 960, 640);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setTitle:@"TellyMedia v4 — Standalone"];
    [_window makeKeyAndOrderFront:nil];

    _contentView = [_window contentView];

    // Get web assets path (next to the executable)
    NSString* exePath = [[NSBundle mainBundle] bundlePath];
    NSString* webPath = [[exePath stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"web"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:webPath]) {
        // Fallback: look in the build directory
        webPath = [exePath stringByDeletingLastPathComponent];
    }

    // Create WKWebView
    _webViewData = TmWebView::Create(_uiData, _contentView, [webPath UTF8String]);

    // Set up command callback
    __block TmUIData* uiData = _uiData;
    __block TmWebViewData* wvd = _webViewData;
    _webViewData->onCommand = [uiData, wvd](const char* json) {
        TmWebView::DispatchCommand(uiData, json);
        // Push updated state back to JS
        std::string stateJson = TmWebView::SerializeState(uiData);
        TmWebView::PushState(wvd, stateJson.c_str());
    };

    // Push initial state
    std::string stateJson = TmWebView::SerializeState(_uiData);
    TmWebView::PushState(_webViewData, stateJson.c_str());

    // Set up resize handling
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(windowResized:)
                                               name:NSWindowDidResizeNotification
                                             object:_window];
}

- (void)windowResized:(NSNotification*)notification {
    NSRect bounds = [_contentView bounds];
    TmWebView::Resize(_webViewData, (int)bounds.size.width, (int)bounds.size.height);
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    if (_uiData) {
        TmUI::SaveState(_uiData);
        delete _uiData;
        _uiData = nullptr;
    }
    if (_host) {
        delete _host;
        _host = nullptr;
    }
    if (_webViewData) {
        TmWebView::Destroy(_webViewData);
        _webViewData = nullptr;
    }
    TmLogger::Shutdown();
}

@end

// ─── Entry point ─────────────────────────────────────────────────────────────
int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        TmAppDelegate* delegate = [[TmAppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}

#endif // VDJ_MAC
