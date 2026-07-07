// TmPluginMac.mm Ã¢â‚¬â€ macOS implementation of TmPlugin
// Replaces the GDI+/D3D11 TmPlugin.cpp with CoreText/OpenGL equivalents.
// This file is compiled instead of TmPlugin.cpp on macOS.

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLCurrent.h>
#include "tm/TmPlugin.h"
#include "tm/TmLogger.h"
#include "tm/TmWebView.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <sys/time.h>
#include <dlfcn.h>

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Static helpers Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

static uint64_t GetTickCountMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static bool DetectKaraokeTrack(const wchar_t* filePath) {
    if (!filePath || !filePath[0]) return false;
    // Simple check for .cdg extension or "karaoke" in path
    wchar_t lowerPath[MAX_PATH];
    wcscpy(lowerPath, filePath);
    // Manual lowercase
    for (wchar_t* p = lowerPath; *p; p++) {
        if (*p >= L'A' && *p <= L'Z') *p = *p - L'A' + L'a';
    }
    const wchar_t* ext = wcsrchr(lowerPath, L'.');
    if (ext && wcscmp(ext, L".cdg") == 0) return true;
    if (wcsstr(lowerPath, L"karaoke") != nullptr) return true;
    return false;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ CoreText text drawing (replaces GDI+ text functions) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

static void DrawTextCoreText(CGContextRef ctx, const wchar_t* text,
                              const wchar_t* fontName, int fontSize,
                              float x, float y, float w, float h,
                              COLORREF textColor, COLORREF textColor2,
                              int align, int valign,
                              COLORREF outlineColor, int outlineWidth,
                              int animMode, float scrollSpeed,
                              double timeSec) {
    if (!text || !text[0] || !ctx) return;

    // Convert wchar_t to CFString
    char utf8[1024];
    wcstombs(utf8, text, sizeof(utf8));
    CFStringRef cfText = CFStringCreateWithCString(kCFAllocatorDefault, utf8, kCFStringEncodingUTF8);
    if (!cfText) return;

    // Convert font name
    char fontNameUtf8[128];
    wcstombs(fontNameUtf8, fontName ? fontName : L"Helvetica", sizeof(fontNameUtf8));
    CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontNameUtf8, kCFStringEncodingUTF8);

    // Create font
    CTFontRef font = CTFontCreateWithName(cfFontName, (CGFloat)std::max(6, fontSize), nullptr);
    CFRelease(cfFontName);

    // Create attributed string
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGFloat components[] = {
        GetRValue(textColor) / 255.0f, GetGValue(textColor) / 255.0f,
        GetBValue(textColor) / 255.0f, 1.0f
    };
    CGColorRef color = CGColorCreate(cs, components);
    CGColorSpaceRelease(cs);

    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef values[] = { font, color };
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault,
        (const void**)keys, (const void**)values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CGColorRelease(color);

    CFAttributedStringRef attrStr = CFAttributedStringCreate(kCFAllocatorDefault, cfText, attrs);
    CFRelease(attrs);
    CFRelease(cfText);

    CTLineRef line = CTLineCreateWithAttributedString(attrStr);
    CFRelease(attrStr);

    // Measure text
    CGFloat textWidth = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);

    // Calculate position based on alignment
    CGFloat drawX = x;
    CGFloat drawY = y + h / 2; // Center vertically by default

    if (align == 1) { // Center
        drawX = x + (w - textWidth) / 2;
    } else if (align == 2) { // Right
        drawX = x + w - textWidth;
    }

    // Handle scrolling animation
    if (animMode == 1 || animMode == 2 || animMode == 3) {
        float speed = scrollSpeed > 0 ? scrollSpeed : 180.0f;
        float offset = fmod(timeSec * speed, w + textWidth);
        if (animMode == 2) { // Bounce
            float cycle = w + textWidth;
            float t = fmod(timeSec * speed / cycle, 2.0f);
            offset = t < 1.0f ? t * cycle : (2.0f - t) * cycle;
        }
        drawX = x + w - offset;
    }

    // Draw outline if specified
    if (outlineWidth > 0) {
        CGColorSpaceRef ocs = CGColorSpaceCreateDeviceRGB();
        CGFloat outlineComp[] = {
            GetRValue(outlineColor) / 255.0f, GetGValue(outlineColor) / 255.0f,
            GetBValue(outlineColor) / 255.0f, 1.0f
        };
        CGColorRef outlineCGColor = CGColorCreate(ocs, outlineComp);
        CGColorSpaceRelease(ocs);

        CGContextSetStrokeColorWithColor(ctx, outlineCGColor);
        CGContextSetLineWidth(ctx, outlineWidth * 2);
        CGContextSetTextDrawingMode(ctx, kCGTextStroke);

        for (int dx = -outlineWidth; dx <= outlineWidth; dx += std::max(1, outlineWidth)) {
            for (int dy = -outlineWidth; dy <= outlineWidth; dy += std::max(1, outlineWidth)) {
                if (dx == 0 && dy == 0) continue;
                CGContextSetTextPosition(ctx, drawX + dx, drawY + dy);
                CTLineDraw(line, ctx);
            }
        }

        CGColorRelease(outlineCGColor);
    }

    // Draw fill text
    CGContextSetTextDrawingMode(ctx, kCGTextFill);
    CGContextSetTextPosition(ctx, drawX, drawY);
    CTLineDraw(line, ctx);

    CFRelease(line);
    CFRelease(font);
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Overlay panel rendering to BGRA buffer (replaces GDI+ version) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

static void RenderPanelToBuffer(const OverlayPanel& p,
                                BYTE* buffer, int bufW, int bufH,
                                const BYTE* slideshowFrame, int ssFrameW, int ssFrameH,
                                double timeSec) {
    if (!buffer || bufW <= 0 || bufH <= 0) return;
    if (!p.visible) return;

    int px = (int)(p.x * bufW);
    int py = (int)(p.y * bufH);
    int pw = (int)(p.w * bufW);
    int ph = (int)(p.h * bufH);
    if (pw <= 0 || ph <= 0) return;

    // Clamp to buffer bounds
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px + pw > bufW) pw = bufW - px;
    if (py + ph > bufH) ph = bufH - py;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        buffer, bufW, bufH, 8, bufW * 4,
        cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(cs);

    if (!ctx) return;

    // Flip Y (CG is bottom-up, we want top-down)
    CGContextTranslateCTM(ctx, 0, bufH);
    CGContextScaleCTM(ctx, 1.0, -1.0);

    // Clip to panel rect
    CGContextClipToRect(ctx, CGRectMake(px, py, pw, ph));

    if (p.panelType == 0) {
        // Main slideshow panel Ã¢â‚¬â€ draw slideshow frame if available
        if (slideshowFrame && ssFrameW > 0 && ssFrameH > 0) {
            CGDataProviderRef dp = CGDataProviderCreateWithData(
                nullptr, slideshowFrame, ssFrameW * ssFrameH * 4, nullptr);
            CGImageRef img = CGImageCreate(
                ssFrameW, ssFrameH, 8, 32, ssFrameW * 4, cs,
                kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Host,
                dp, nullptr, false, kCGRenderingIntentDefault);
            CGDataProviderRelease(dp);
            if (img) {
                CGContextDrawImage(ctx, CGRectMake(px, py, pw, ph), img);
                CGImageRelease(img);
            }
        }
    } else if (p.panelType == 1) {
        // Image panel Ã¢â‚¬â€ draw loaded image if available
        // (Image data is in m_overlayImagePixels Ã¢â‚¬â€ handled by caller)
    } else if (p.panelType == 2) {
        // Text panel
        DrawTextCoreText(ctx, p.textContent, p.fontName, p.fontSize,
                         (float)px, (float)py, (float)pw, (float)ph,
                         p.textColor, p.textColor2,
                         1, 1, // Center aligned
                         p.outlineColor, p.outlineWidth,
                         p.textAnimMode, p.scrollSpeed, timeSec);
    } else if (p.panelType == 4) {
        // Song / Now Playing panel
        // Draw background if specified
        if (p.hasBgColor && p.bgOpacity > 0) {
            CGFloat bgAlpha = p.bgOpacity / 100.0f;
            CGContextSetRGBFillColor(ctx,
                GetRValue(p.bgColor) / 255.0f,
                GetGValue(p.bgColor) / 255.0f,
                GetBValue(p.bgColor) / 255.0f, bgAlpha);
            CGContextFillRect(ctx, CGRectMake(px, py, pw, ph));
        }

        // Draw song text (the caller sets p.textContent to the now-playing string)
        DrawTextCoreText(ctx, p.textContent, p.songFontName, p.fontSize,
                         (float)px, (float)py, (float)pw, (float)ph,
                         p.songTextColor, p.songTextColor2,
                         1, 1,
                         p.outlineColor, p.outlineWidth,
                         p.textAnimMode, p.scrollSpeed, timeSec);
    }

    CGContextRelease(ctx);
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ TmPlugin implementation Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

TmPlugin::TmPlugin()
    : m_settingsWnd(nullptr),
      m_glContext(nullptr), m_d3dReady(false),
      m_nOpenFile(0), m_nPlay(0), m_nStop(0), m_nPause(0),
      m_fVolume(1.0f), m_fAlpha(1.0f), m_bLooping(1),
      m_playing(false), m_scaleMode(SCALE_ASPECT),
      m_transitionMode(0), m_transitionAlpha(1.0f),
      m_curBank(0), m_curSlot(-1),
      m_numOverlayPanels(0),
      m_renderModeRequested(TM_RENDER_AUTO), m_renderModeEffective(TM_RENDER_CPU), m_renderAvgMs(0.0f)
{
    m_currentPath[0] = 0;
    m_songTitle[0] = 0;
    m_songArtist[0] = 0;
    m_songDisplay[0] = 0;
    ZeroMemory(m_overlayPanels, sizeof(m_overlayPanels));
    ZeroMemory(m_overlayImages, sizeof(m_overlayImages));
    ZeroMemory(m_overlayImageTexs, sizeof(m_overlayImageTexs));
    ZeroMemory(m_overlayImagePixelW, sizeof(m_overlayImagePixelW));
    ZeroMemory(m_overlayImagePixelH, sizeof(m_overlayImagePixelH));

    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) {
        m_overlayPanels[i].fontWeight = 1;
        m_overlayPanels[i].fontStyle = 0;
        m_overlayPanels[i].bgStyle = 0;
        m_overlayPanels[i].bgColor2 = RGB(0, 0, 0);
        m_overlayPanels[i].cornerRadius = 0;
        m_overlayPanels[i].shadowBlur = 0;
        m_overlayPanels[i].shadowColor = RGB(0, 0, 0);
        m_overlayPanels[i].layoutMode = 1;
        wcscpy(m_overlayPanels[i].fontName, L"Helvetica");
        m_overlayPanels[i].textColor = RGB(255, 255, 255);
        m_overlayPanels[i].textColor2 = m_overlayPanels[i].textColor;
        wcscpy(m_overlayPanels[i].headingFontName, L"Helvetica");
        m_overlayPanels[i].headingTextColor = RGB(255, 255, 255);
        m_overlayPanels[i].headingTextColor2 = m_overlayPanels[i].headingTextColor;
        wcscpy(m_overlayPanels[i].songFontName, L"Helvetica");
        m_overlayPanels[i].songTextColor = RGB(255, 255, 255);
        m_overlayPanels[i].songTextColor2 = m_overlayPanels[i].songTextColor;
        m_overlayPanels[i].showTitle = true;
        m_overlayPanels[i].showArtist = true;
        m_overlayPanels[i].showAlbum = false;
        m_overlayPanels[i].showBPM = false;
        m_overlayPanels[i].showTime = false;
        m_overlayPanels[i].showProgress = false;
        m_overlayPanels[i].progressColor = RGB(0, 255, 128);
        m_overlayPanels[i].progressBgColor = RGB(64, 64, 64);
        m_overlayPanels[i].scrollSpeed = 300.0f;
        m_overlayPanels[i].outlineColor = RGB(0, 0, 0);
        m_overlayPanels[i].outlineWidth = 0;
        m_overlayPanels[i].textAnimMode = 0;
    }

    m_slideshowFrameW = 0;
    m_slideshowFrameH = 0;
    m_slideshowFrameValid = false;
    m_slideshowPrevW = 0;
    m_slideshowPrevH = 0;
    m_slideshowPrevValid = false;
    m_transitionProgress = 1.0f;

    // Initialize pthread mutexes
    pthread_mutex_t* overlayMtx = new pthread_mutex_t;
    pthread_mutex_init(overlayMtx, nullptr);
    m_overlayLock = overlayMtx;
    m_overlayLockInit = true;

    ZeroMemory(&m_audio, sizeof(m_audio));
    m_audio.sampleRate = 44100;
    pthread_mutex_t* audioMtx = new pthread_mutex_t;
    pthread_mutex_init(audioMtx, nullptr);
    m_audioLock = audioMtx;
    m_audioLockInit = true;

    m_shadersEnabled = false;
    m_shaderDisableOnKaraoke = true;
    m_karaokeAutoHide = true;
    m_mainShaderIndex = 0;
    m_isKaraoke = false;
    m_isAudioOnly = false;
    m_karaokeCheckFrame = 0;
    m_audioOnlyCheckFrame = 0;

    TmLicense::Init(&m_license);
    m_media.Init();
}

TmPlugin::~TmPlugin() {
    if (m_settingsWnd) {
        TmWebView::Destroy((TmWebViewData*)m_settingsWnd);
        m_settingsWnd = nullptr;
    }
    m_media.Shutdown();

    if (m_overlayLockInit) {
        pthread_mutex_t* mtx = (pthread_mutex_t*)m_overlayLock;
        pthread_mutex_destroy(mtx);
        delete mtx;
        m_overlayLock = nullptr;
        m_overlayLockInit = false;
    }
    if (m_audioLockInit) {
        pthread_mutex_t* mtx = (pthread_mutex_t*)m_audioLock;
        pthread_mutex_destroy(mtx);
        delete mtx;
        m_audioLock = nullptr;
        m_audioLockInit = false;
    }
}

void TmPlugin::RefreshNowPlayingCache() {
    m_songTitle[0] = L'\0';
    m_songArtist[0] = L'\0';
    m_songDisplay[0] = L'\0';
    m_currentPath[0] = L'\0';

    char titleA[256] = {};
    char artistA[256] = {};
    char pathA[MAX_PATH] = {};

    if (SUCCEEDED(GetStringInfo("get_loaded_song 'title'", titleA, sizeof(titleA)))) {
        mbstowcs(m_songTitle, titleA, 255);
    }
    if (SUCCEEDED(GetStringInfo("get_loaded_song 'artist'", artistA, sizeof(artistA)))) {
        mbstowcs(m_songArtist, artistA, 255);
    }
    if (SUCCEEDED(GetStringInfo("get_loaded_song 'path'", pathA, sizeof(pathA)))) {
        mbstowcs(m_currentPath, pathA, MAX_PATH - 1);
    }

    // Build display string
    if (m_songTitle[0] && m_songArtist[0]) {
        swprintf(m_songDisplay, sizeof(m_songDisplay)/sizeof(m_songDisplay[0]),
                 L"%s - %s", m_songArtist, m_songTitle);
    } else if (m_songTitle[0]) {
        wcscpy(m_songDisplay, m_songTitle);
    } else if (m_songArtist[0]) {
        wcscpy(m_songDisplay, m_songArtist);
    } else {
        wcscpy(m_songDisplay, L"No Track Playing");
    }
}

void TmPlugin::ClearOverlayImageCache() {
    pthread_mutex_lock((pthread_mutex_t*)m_overlayLock);
    ReleaseOverlayPanelTextures();
    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) {
        if (m_overlayImages[i]) {
            CGImageRelease((CGImageRef)m_overlayImages[i]);
            m_overlayImages[i] = nullptr;
        }
        m_overlayImagePixels[i].clear();
        m_overlayImagePixelW[i] = 0;
        m_overlayImagePixelH[i] = 0;
    }
    pthread_mutex_unlock((pthread_mutex_t*)m_overlayLock);
}

void TmPlugin::ReleaseOverlayPanelTextures() {
    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) {
        if (m_overlayImageTexs[i]) {
            glDeleteTextures(1, &m_overlayImageTexs[i]);
            m_overlayImageTexs[i] = 0;
        }
    }
}

void TmPlugin::BuildOverlayPanelTextures() {
    pthread_mutex_lock((pthread_mutex_t*)m_overlayLock);
    ReleaseOverlayPanelTextures();

    for (int i = 0; i < m_numOverlayPanels; ++i) {
        if (m_overlayPanels[i].panelType == 1 && m_overlayImages[i]) {
            // Create OpenGL texture from CGImage
            CGImageRef img = (CGImageRef)m_overlayImages[i];
            int w = (int)CGImageGetWidth(img);
            int h = (int)CGImageGetHeight(img);

            // Allocate pixel buffer
            m_overlayImagePixels[i].resize(w * h * 4);
            m_overlayImagePixelW[i] = w;
            m_overlayImagePixelH[i] = h;

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                m_overlayImagePixels[i].data(), w, h, 8, w * 4,
                cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Host);
            CGColorSpaceRelease(cs);

            CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
            CGContextRelease(ctx);

            // Create GL texture
            glGenTextures(1, &m_overlayImageTexs[i]);
            glBindTexture(GL_TEXTURE_2D, m_overlayImageTexs[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, m_overlayImagePixels[i].data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    pthread_mutex_unlock((pthread_mutex_t*)m_overlayLock);
}

void TmPlugin::DrawOverlayImagePanels() {
    pthread_mutex_lock((pthread_mutex_t*)m_overlayLock);

    int sorted[MAX_OVERLAY_PANELS];
    int sortedCount = 0;
    for (int i = 0; i < m_numOverlayPanels; ++i) {
        if (m_overlayPanels[i].panelType == 1 && m_overlayPanels[i].visible && m_overlayImageTexs[i]) {
            sorted[sortedCount++] = i;
        }
    }

    // Sort by zOrder
    std::sort(sorted, sorted + sortedCount, [&](int a, int b) {
        return m_overlayPanels[a].zOrder < m_overlayPanels[b].zOrder;
    });

    for (int s = 0; s < sortedCount; ++s) {
        int i = sorted[s];
        const OverlayPanel& p = m_overlayPanels[i];
        m_renderer.DrawRectWithTexture(p.x, p.y, p.w, p.h, 1.0f, m_overlayImageTexs[i]);
    }

    pthread_mutex_unlock((pthread_mutex_t*)m_overlayLock);
}

float TmPlugin::EstimateOverlayImageLoad() const {
    if (width <= 0 || height <= 0) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < m_numOverlayPanels; ++i) {
        if (m_overlayPanels[i].panelType == 1 && m_overlayPanels[i].visible && m_overlayImageTexs[i]) {
            int w = m_overlayImagePixelW[i];
            int h = m_overlayImagePixelH[i];
            total += (float)(w * h * 4) / (width * height * 4);
        }
    }
    return total;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ IVdjPlugin8 Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

HRESULT VDJ_API TmPlugin::OnLoad() {
    TM_SEP();
    TM_INFO("TmPlugin::OnLoad (macOS)");

    DeclareParameterButton(&m_nOpenFile, PARAM_OPEN_FILE, "Open File", "Open");
    DeclareParameterButton(&m_nPlay,     PARAM_PLAY,      "Play",      "Play");
    DeclareParameterButton(&m_nStop,     PARAM_STOP,      "Stop",      "Stop");
    DeclareParameterButton(&m_nPause,    PARAM_PAUSE,     "Pause",     "Pause");
    DeclareParameterSlider(&m_fVolume,    PARAM_VOLUME,    "Volume",    "Volume", 1.0f);
    DeclareParameterSlider(&m_fAlpha,     PARAM_ALPHA,     "Alpha",     "Alpha", 1.0f);
    DeclareParameterSwitch(&m_bLooping,   PARAM_LOOP,      "Loop",      "Loop", 1);

    // Load saved state
    TmLicense::LoadFromRegistry(&m_license);
    TmLicense::LoadSecureCredentials(&m_license);
    if (m_license.licenseKey[0] && m_license.authToken[0]) {
        TmLicense::Validate(&m_license);
    }

    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnGetPluginInfo(TVdjPluginInfo8* info) {
    info->PluginName  = "TellyMedia v4";
    info->Author      = "DJ Micky K";
    info->Description = "Media overlay plugin for VirtualDJ (macOS)";
    info->Version     = "4.0.0";
    info->Bitmap      = nullptr;
    // info->DefaultVideoEngine = VdjVideoEngineOpenGL; // Not in SDK
    return S_OK;
}

ULONG VDJ_API TmPlugin::Release() { delete this; return S_OK; }

HRESULT VDJ_API TmPlugin::OnParameter(int id) {
    switch (id) {
    case PARAM_PLAY:  if (m_nPlay)  m_playing = true;  break;
    case PARAM_STOP:  if (m_nStop)  { m_playing = false; m_media.Stop(); } break;
    case PARAM_PAUSE: if (m_nPause) { m_playing = false; m_media.Pause(); } break;
    case PARAM_OPEN_FILE:
        if (m_nOpenFile) {
            // File open is handled via the UI webview
        }
        break;
    }
    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnGetUserInterface(TVdjPluginInterface8* pi) {
    // macOS: Return dimensions for the embedded WKWebView settings panel
    // VirtualDJ creates the parent NSView; we embed our WKWebView into it
    // when the panel is shown via TmWebView::Create.
    pi->hWnd = nullptr; // Will be created on first show
    // pi->Width = 960;   // Not in SDK struct
    // pi->Height = 640;  // Not in SDK struct
    return S_OK;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ IVdjPluginVideoFx8 Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

HRESULT VDJ_API TmPlugin::OnStart() { TM_INFO("OnStart"); return S_OK; }
HRESULT VDJ_API TmPlugin::OnStop()  { TM_INFO("OnStop");  return S_OK; }

HRESULT VDJ_API TmPlugin::OnDeviceInit() {
    TM_SEP();
    TM_INFO("OnDeviceInit (macOS) - output %dx%d", width, height);

    CGLContextObj ctx = nullptr;
    HRESULT hr = GetDevice(VdjVideoEngineOpenGL, (void**)&ctx);
    if (FAILED(hr) || !ctx) {
        TM_ERROR("No OpenGL context from VDJ - OnDraw will pass deck through");
        return S_OK;
    }
    m_glContext = ctx;

    m_d3dReady = m_renderer.Init(ctx);
    TM_INFO("OnDeviceInit complete - rendererReady=%s", m_d3dReady ? "YES" : "NO");
    BuildOverlayPanelTextures();

    // Load custom shaders from shaders folder next to bundle
    // In a .bundle loaded by VirtualDJ, mainBundle points to VDJ.
    // Use the image path of this bundle's executable instead.
    Dl_info info;
    if (dladdr((void*)DllGetClassObject, &info) && info.dli_fname) {
        NSString* exePath = [NSString stringWithUTF8String:info.dli_fname];
        NSString* bundleDir = [[exePath stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
        NSString* shadersDir = [bundleDir stringByAppendingPathComponent:@"shaders"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:shadersDir]) {
            shadersDir = [[exePath stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"shaders"];
        }
        wchar_t wPath[MAX_PATH];
        mbstowcs(wPath, [shadersDir UTF8String], MAX_PATH - 1);
        m_renderer.LoadShaders(wPath);
    }

    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnDeviceClose() {
    TM_INFO("OnDeviceClose (macOS)");
    ReleaseOverlayPanelTextures();
    m_renderer.Shutdown();
    m_glContext = nullptr;
    m_d3dReady = false;
    return S_OK;
}

// Simple 2nd-order IIR bandpass filter (same as Windows)
static float ProcessBand(float sample, float* state, float freq, float q, float sampleRate) {
    float w0 = 2.0f * 3.14159265f * freq / sampleRate;
    float alpha = sinf(w0) / (2.0f * q);
    float b0 = alpha, b1 = 0.0f, b2 = -alpha;
    float a0 = 1.0f + alpha, a1 = -2.0f * cosf(w0), a2 = 1.0f - alpha;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    float out = b0 * sample + b1 * state[0] + b2 * state[1] - a1 * state[2] - a2 * state[3];
    state[3] = state[2]; state[2] = out;
    state[1] = state[0]; state[0] = sample;
    return out;
}

HRESULT VDJ_API TmPlugin::OnAudioSamples(float* buf, int nb) {
    if (!buf || nb <= 0) return S_OK;

    pthread_mutex_lock((pthread_mutex_t*)m_audioLock);

    m_audio.sampleRate = SampleRate > 0 ? SampleRate : 44100;

    float sumSq = 0.0f;
    for (int i = 0; i < nb; ++i) sumSq += buf[i] * buf[i];
    float rms = sqrtf(sumSq / nb);

    const float attack = 0.1f, decay = 0.001f;
    if (rms > m_audio.level) {
        m_audio.level = m_audio.level + (rms - m_audio.level) * attack;
    } else {
        m_audio.level = m_audio.level + (rms - m_audio.level) * decay;
    }

    m_audio.bass = m_audio.level;
    m_audio.mid = m_audio.level;
    m_audio.treble = m_audio.level;

    // Simple beat detection
    static float avgLevel = 0.0f;
    avgLevel = avgLevel * 0.95f + rms * 0.05f;
    if (rms > avgLevel * 1.5f && rms > 0.05f) {
        m_audio.beat = 1.0f;
    } else {
        m_audio.beat *= 0.9f;
    }

    pthread_mutex_unlock((pthread_mutex_t*)m_audioLock);
    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnDraw() {
    static int drawCount = 0;
    drawCount++;

    if (!m_d3dReady) {
        DrawDeck();
        return S_OK;
    }

    // Find the visible Main Panel
    bool hasMainPanel = false;
    float mainX = 0, mainY = 0, mainW = 1, mainH = 1;
    for (int i = 0; i < m_numOverlayPanels; ++i) {
        if (m_overlayPanels[i].panelType == 0 && m_overlayPanels[i].visible) {
            float x = m_overlayPanels[i].x, y = m_overlayPanels[i].y;
            float w = m_overlayPanels[i].w, h = m_overlayPanels[i].h;
            if (w < 0.01f) w = 0.01f;
            if (h < 0.01f) h = 0.01f;
            if (x < 0.f) x = 0.f;
            if (y < 0.f) y = 0.f;
            if (x + w > 1.f) w = 1.f - x;
            if (y + h > 1.f) h = 1.f - y;
            hasMainPanel = true;
            mainX = x; mainY = y; mainW = w; mainH = h;
            break;
        }
    }

    pthread_mutex_lock((pthread_mutex_t*)m_overlayLock);

    // Get VDJ's deck texture
    TVertex* deckVtx = nullptr;
    GLuint deckTex = 0;
    GetTexture(VdjVideoEngineOpenGL, (void**)&deckTex, &deckVtx);

    // Karaoke detection (throttled)
    if (drawCount - m_karaokeCheckFrame > 30) {
        m_karaokeCheckFrame = drawCount;
        RefreshNowPlayingCache();

        bool isKaraoke = false;
        char karaokeA[32] = {};
        if (SUCCEEDED(GetStringInfo("get_karaoke ? on : off", karaokeA, sizeof(karaokeA)))) {
            // lowercase
            for (char* p = karaokeA; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            isKaraoke = (strcmp(karaokeA, "on") == 0);
        }
        if (!isKaraoke && m_currentPath[0]) {
            isKaraoke = DetectKaraokeTrack(m_currentPath);
        }
        m_isKaraoke = isKaraoke;
    }

    // Audio-only detection
    if (drawCount - m_audioOnlyCheckFrame > 30) {
        m_audioOnlyCheckFrame = drawCount;
        m_isAudioOnly = !TmMedia::IsVideoPath(m_currentPath);
        if (!deckTex) m_isAudioOnly = true;
    }

    const bool karaokeBlocking = m_karaokeAutoHide && m_isKaraoke;

    // Render deck video into main panel
    if (!karaokeBlocking && hasMainPanel && deckVtx) {
        float outW = (float)width, outH = (float)height;
        float pxX1 = mainX * outW, pxY1 = mainY * outH;
        float pxX2 = (mainX + mainW) * outW, pxY2 = (mainY + mainH) * outH;

        float origX[4], origY[4];
        for (int i = 0; i < 4; ++i) {
            origX[i] = deckVtx[i].position.x;
            origY[i] = deckVtx[i].position.y;
        }
        deckVtx[0].position.x = pxX1; deckVtx[0].position.y = pxY1;
        deckVtx[1].position.x = pxX2; deckVtx[1].position.y = pxY1;
        deckVtx[2].position.x = pxX2; deckVtx[2].position.y = pxY2;
        deckVtx[3].position.x = pxX1; deckVtx[3].position.y = pxY2;
        DrawDeck();
        for (int i = 0; i < 4; ++i) {
            deckVtx[i].position.x = origX[i];
            deckVtx[i].position.y = origY[i];
        }
    } else {
        DrawDeck();
    }

    // Capture media frame for slideshow
    if (!karaokeBlocking && m_playing) {
        const void* pixels = nullptr;
        int fw = 0, fh = 0, fstride = 0;
        if (m_media.BeginFrame(&pixels, &fw, &fh, &fstride) && fw > 0 && fh > 0) {
            int targetW = hasMainPanel ? (int)(mainW * width) : 1920;
            int targetH = hasMainPanel ? (int)(mainH * height) : 1080;

            m_slideshowFrameCache.resize(targetW * targetH * 4);
            // Simple nearest-neighbor resize
            for (int y = 0; y < targetH; y++) {
                int srcY = y * fh / targetH;
                const BYTE* srcRow = (const BYTE*)pixels + srcY * fstride;
                BYTE* dstRow = m_slideshowFrameCache.data() + y * targetW * 4;
                for (int x = 0; x < targetW; x++) {
                    int srcX = x * fw / targetW;
                    memcpy(dstRow + x * 4, srcRow + srcX * 4, 4);
                }
            }
            m_slideshowFrameW = targetW;
            m_slideshowFrameH = targetH;
            m_slideshowFrameValid = true;
        }
        m_media.EndFrame();
    }

    // Render overlay panels to buffer
    if (m_numOverlayPanels > 0 && !karaokeBlocking) {
        int overlayW = width, overlayH = height;
        std::vector<BYTE> overlayBuf(overlayW * overlayH * 4, 0);

        double timeSec = (double)GetTickCountMs() * 0.001;

        for (int i = 0; i < m_numOverlayPanels; ++i) {
            const OverlayPanel& p = m_overlayPanels[i];
            if (!p.visible) continue;
            if (p.panelType == 1) continue; // Image panels drawn separately
            if (p.panelType == 3) continue; // Shader panels drawn separately

            RenderPanelToBuffer(p, overlayBuf.data(), overlayW, overlayH,
                               m_slideshowFrameValid ? m_slideshowFrameCache.data() : nullptr,
                               m_slideshowFrameW, m_slideshowFrameH, timeSec);
        }

        // Upload overlay buffer to OpenGL texture and draw
        m_renderer.UploadOverlay(overlayBuf.data(), overlayW, overlayH, overlayW * 4);
        m_renderer.DrawOverlay();
    }

    // Draw image panels
    if (!karaokeBlocking) {
        DrawOverlayImagePanels();
    }

    // Draw shader panels
    if (!karaokeBlocking && m_shadersEnabled && m_renderer.GetShaderCount() > 0) {
        bool shaderAllowed = !(m_shaderDisableOnKaraoke && m_isKaraoke);
        if (shaderAllowed) {
            for (int i = 0; i < m_numOverlayPanels; ++i) {
                if (m_overlayPanels[i].panelType == 3 && m_overlayPanels[i].visible) {
                    TmRenderer::ShaderUniforms uniforms;
                    uniforms.iResolution[0] = (float)width;
                    uniforms.iResolution[1] = (float)height;
                    uniforms.iTime = (float)time(nullptr);
                    pthread_mutex_lock((pthread_mutex_t*)m_audioLock);
                    uniforms.iBeat = m_audio.beat;
                    uniforms.iLevel = m_audio.level;
                    uniforms.iBass = m_audio.bass;
                    uniforms.iMid = m_audio.mid;
                    uniforms.iTreble = m_audio.treble;
                    pthread_mutex_unlock((pthread_mutex_t*)m_audioLock);
                    uniforms.iBpm = 120.0f;
                    uniforms.iSongPosBeats = 0.0f;

                    int shaderIdx = m_mainShaderIndex % m_renderer.GetShaderCount();
                    m_renderer.DrawShaderInRect(shaderIdx,
                        m_overlayPanels[i].x, m_overlayPanels[i].y,
                        m_overlayPanels[i].w, m_overlayPanels[i].h, uniforms);
                }
            }
        }
    }

    pthread_mutex_unlock((pthread_mutex_t*)m_overlayLock);
    return S_OK;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ ITmUIHost (UI -> plugin) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

void TmPlugin::HostPlayMedia(const wchar_t* path, int scaleMode) {
    if (!path || !path[0]) return;
    TM_INFO("HostPlayMedia: %S (scale=%d)", path, scaleMode);
    if (!m_media.Open(path, scaleMode)) {
        TM_WARN("HostPlayMedia: failed to open %S", path);
    }
    m_playing = true;
}

void TmPlugin::HostStop() {
    m_playing = false;
    m_media.Stop();
    pthread_mutex_lock((pthread_mutex_t*)m_overlayLock);
    m_slideshowFrameValid = false;
    m_slideshowPrevValid = false;
    m_transitionProgress = 1.0f;
    pthread_mutex_unlock((pthread_mutex_t*)m_overlayLock);
}

void TmPlugin::HostPause() { m_playing = false; m_media.Pause(); }
void TmPlugin::HostScaleMode(int mode) { m_scaleMode = mode; }
void TmPlugin::HostTransitionMode(int mode) { m_transitionMode = mode; }
void TmPlugin::HostSlideshowStart() { TM_INFO("UI slideshow start"); }
void TmPlugin::HostSlideshowStop() { TM_INFO("UI slideshow stop"); HostStop(); }
void TmPlugin::HostRenderMode(int mode) {
    if (mode < TM_RENDER_AUTO || mode > TM_RENDER_GPU) mode = TM_RENDER_AUTO;
    m_renderModeRequested = mode;
}

void TmPlugin::HostGetNowPlayingText(wchar_t* outText, int outTextMax) {
    if (!outText || outTextMax <= 0) return;
    RefreshNowPlayingCache();
    wcscpy(outText, m_songDisplay[0] ? m_songDisplay : L"No Track Playing");
}

void TmPlugin::HostGetRenderModeText(wchar_t* outText, int outTextMax) {
    if (!outText || outTextMax <= 0) return;
    const wchar_t* mode = L"Auto";
    switch (m_renderModeEffective) {
    case TM_RENDER_CPU: mode = L"CPU"; break;
    case TM_RENDER_GPU: mode = L"GPU"; break;
    }
    wcscpy(outText, mode);
}

void TmPlugin::HostSetShadersEnabled(bool enabled) { m_shadersEnabled = enabled; }
void TmPlugin::HostSetShaderKaraokeDisable(bool disable) { m_shaderDisableOnKaraoke = disable; }
void TmPlugin::HostSetKaraokeAutoHide(bool enabled) { m_karaokeAutoHide = enabled; }
void TmPlugin::HostSetMainShader(int index) { m_mainShaderIndex = index; }

void TmPlugin::HostReloadShaders() {
    Dl_info info;
    if (dladdr((void*)DllGetClassObject, &info) && info.dli_fname) {
        NSString* exePath = [NSString stringWithUTF8String:info.dli_fname];
        NSString* bundleDir = [[exePath stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
        NSString* shadersDir = [bundleDir stringByAppendingPathComponent:@"shaders"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:shadersDir]) {
            shadersDir = [[exePath stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"shaders"];
        }
        wchar_t wPath[MAX_PATH];
        mbstowcs(wPath, [shadersDir UTF8String], MAX_PATH - 1);
        m_renderer.ReloadShaders(wPath);
    }
    TM_INFO("Shaders reloaded");
}

void TmPlugin::HostGetShaderNames(wchar_t* outNames, int outNamesMax) {
    if (!outNames || outNamesMax <= 0) return;
    outNames[0] = L'\0';
    int count = m_renderer.GetShaderCount();
    for (int i = 0; i < count && outNamesMax > 1; ++i) {
        const wchar_t* name = m_renderer.GetShaderName(i);
        int len = (int)wcslen(name);
        if (len >= outNamesMax) len = outNamesMax - 1;
        wcsncpy(outNames, name, len);
        outNames += len;
        outNamesMax -= len;
        if (outNamesMax > 1) { *outNames = L'|'; outNames++; outNamesMax--; }
    }
    if (outNames[0]) outNames[-1] = L'\0'; // Remove trailing separator
}

bool TmPlugin::HostGetLicenseState(LicenseState* outState) {
    if (!outState) return false;
    *outState = m_license;
    return true;
}

bool TmPlugin::HostLicenseLogin(const wchar_t* email, const wchar_t* password, bool savePassword) {
    m_license.savePassword = savePassword;
    const bool ok = TmLicense::Login(&m_license, email, password);
    TM_INFO("License login result: %s", ok ? "OK" : "FAILED");
    return ok;
}

bool TmPlugin::HostLicenseValidate() {
    const bool ok = TmLicense::Validate(&m_license);
    TM_INFO("License validate result: %s", ok ? "OK" : "FAILED");
    return ok;
}

bool TmPlugin::HostLicenseActivate() {
    const bool ok = TmLicense::ActivateDevice(&m_license);
    TM_INFO("License activate result: %s", ok ? "OK" : "FAILED");
    return ok;
}

void TmPlugin::HostLicenseLogout() {
    TmLicense::Logout(&m_license);
}

void TmPlugin::UISetOverlayPanels(const OverlayPanel* panels, int count) {
    pthread_mutex_lock((pthread_mutex_t*)m_overlayLock);
    if (panels && count > 0 && count <= MAX_OVERLAY_PANELS) {
        memcpy(m_overlayPanels, panels, count * sizeof(OverlayPanel));
        m_numOverlayPanels = count;
    } else {
        m_numOverlayPanels = 0;
    }
    pthread_mutex_unlock((pthread_mutex_t*)m_overlayLock);
    BuildOverlayPanelTextures();
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ RenderPanelsToBuffer (macOS version) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

void TmPlugin::RenderPanelsToBuffer(const OverlayPanel* panels,
                                     CGImageRef* images,
                                     GLuint const* panelTexs,
                                     int count, BYTE* buffer, int bufW, int bufH,
                                     bool skipGpuImagePanels,
                                     const BYTE* slideshowFrame, int ssFrameW, int ssFrameH) {
    if (!panels || !buffer || bufW <= 0 || bufH <= 0) return;

    double timeSec = (double)GetTickCountMs() * 0.001;

    for (int i = 0; i < count; ++i) {
        const OverlayPanel& p = panels[i];
        if (!p.visible) continue;
        if (skipGpuImagePanels && p.panelType == 1 && panelTexs && panelTexs[i]) continue;
        if (p.panelType == 3) continue; // Shader panels handled separately

        RenderPanelToBuffer(p, buffer, bufW, bufH, slideshowFrame, ssFrameW, ssFrameH, timeSec);
    }
}

#endif // VDJ_MAC
