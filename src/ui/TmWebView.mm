// TmWebView.mm Ã¢â‚¬â€ WKWebView host for macOS
// Creates a WKWebView, loads the TellyMedia HTML UI, and bridges messages
// between JavaScript and C++ via WKScriptMessageHandler.

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "tm/TmWebView.h"
#include "tm/TmLogger.h"

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ ObjC delegate that receives messages from JS Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
@interface TmWebViewDelegate : NSObject <WKScriptMessageHandler, WKNavigationDelegate>
@property (nonatomic, assign) TmWebViewData* data;
@end

@implementation TmWebViewDelegate

- (void)userContentController:(WKUserContentController*)ucc
      didReceiveScriptMessage:(WKScriptMessage*)message {
    if (![message.name isEqualToString:@"tmBridge"]) return;
    NSString* body = [message body isKindOfClass:[NSString class]]
                     ? (NSString*)[message body]
                     : [[NSString alloc] initWithData:(NSData*)[message body]
                                             encoding:NSUTF8StringEncoding];
    if (!body) return;
    const char* json = [body UTF8String];
    TM_INFO("JS cmd: %s", json);

    if (_data && _data->onCommand) {
        _data->onCommand(json);
    }
}

- (void)webView:(WKWebView*)webView
      didFinishNavigation:(WKNavigation*)navigation {
    TM_INFO("WebView finished loading");
    // Push initial state if available
    if (_data && _data->needsStatePush) {
        [_data->webView evaluateJavaScript:
            [NSString stringWithFormat:@"window.tmOnMessage('%s');",
                _data->pendingStateJson.c_str()]
            completionHandler:nil];
        _data->needsStatePush = false;
    }
}

@end

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ C++ implementation Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

namespace TmWebView {

TmWebViewData* Create(TmUIData* uiData, NSView parentView, const char* webPath) {
    TmWebViewData* data = new TmWebViewData;
    data->uiData = uiData;
    data->parentView = parentView;
    data->needsStatePush = false;

    // Create WKWebView configuration
    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    WKUserContentController* ucc = [[WKUserContentController alloc] init];

    // Register message handler for JS -> C++ bridge
    TmWebViewDelegate* delegate = [[TmWebViewDelegate alloc] init];
    delegate.data = data;
    data->delegate = delegate;
    [ucc addScriptMessageHandler:delegate name:@"tmBridge"];

    config.userContentController = ucc;
    config.preferences.javaScriptEnabled = YES;

    // Create WKWebView
    NSRect frame = [(NSView*)parentView bounds];
    WKWebView* webView = [[WKWebView alloc] initWithFrame:frame configuration:config];
    webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    webView.navigationDelegate = delegate;
    data->webView = webView;

    // Load index.html from the web folder
    NSString* webDir = [NSString stringWithUTF8String:webPath];
    NSString* htmlPath = [webDir stringByAppendingPathComponent:@"index.html"];
    NSURL* url = [NSURL fileURLWithPath:htmlPath];
    NSURLRequest* req = [NSURLRequest requestWithURL:url];
    [webView loadRequest:req];

    // Embed in parent view
    [(NSView*)parentView addSubview:webView];

    TM_INFO("WKWebView created, loading: %s", [htmlPath UTF8String]);
    return data;
}

void Destroy(TmWebViewData* data) {
    if (!data) return;
    if (data->webView) {
        [(WKWebView*)data->webView removeFromSuperview];
        data->webView = nil;
    }
    if (data->delegate) {
        // Remove script message handler to break retain cycle
        WKUserContentController* ucc = [(WKWebView*)data->webView configuration].userContentController;
        [ucc removeScriptMessageHandlerForName:@"tmBridge"];
        data->delegate = nil;
    }
    delete data;
}

void PushState(TmWebViewData* data, const char* json) {
    if (!data || !data->webView || !json) return;
    NSString* js = [NSString stringWithFormat:@"window.tmOnMessage(%s);", json];
    [(WKWebView*)data->webView evaluateJavaScript:js completionHandler:nil];
}

void PushThumbnail(TmWebViewData* data, int bank, int slot, const char* base64Data) {
    if (!data || !data->webView || !base64Data) return;
    NSString* js = [NSString stringWithFormat:
        @"window.tmOnMessage(JSON.stringify({type:'thumb',slot:%d,bank:%d,data:'%s'}));",
        slot, bank, base64Data];
    [(WKWebView*)data->webView evaluateJavaScript:js completionHandler:nil];
}

void PushPanelImage(TmWebViewData* data, int panelIndex, const char* base64Data) {
    if (!data || !data->webView || !base64Data) return;
    NSString* js = [NSString stringWithFormat:
        @"window.tmOnMessage(JSON.stringify({type:'panelImage',index:%d,data:'%s'}));",
        panelIndex, base64Data];
    [(WKWebView*)data->webView evaluateJavaScript:js completionHandler:nil];
}

void PushEvent(TmWebViewData* data, const char* name, const char* jsonData) {
    if (!data || !data->webView || !name) return;
    NSString* js = [NSString stringWithFormat:
        @"window.tmOnMessage(JSON.stringify({type:'event',name:'%s',data:%s}));",
        name, jsonData ? jsonData : "null"];
    [(WKWebView*)data->webView evaluateJavaScript:js completionHandler:nil];
}

void Resize(TmWebViewData* data, int width, int height) {
    if (!data || !data->webView) return;
    NSRect frame = NSMakeRect(0, 0, width, height);
    [(WKWebView*)data->webView setFrame:frame];
}

} // namespace TmWebView

#endif // VDJ_MAC
