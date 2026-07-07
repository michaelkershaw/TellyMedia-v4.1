// TmMediaMac.mm â€” AVFoundation-based media engine for macOS
// Replaces the Media Foundation / GDI+ TmMedia.cpp.
// Uses CoreGraphics for images, AVFoundation for video.

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#include "tm/TmMedia.h"
#include "tm/TmLogger.h"
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <cwctype>

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Helper: wchar_t path to NSString Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡
static NSString* WCharToNSString(const wchar_t* ws) {
    if (!ws || !ws[0]) return @"";
    size_t len = wcslen(ws);
    std::string utf8;
    utf8.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        if (ws[i] < 128) {
            utf8 += (char)ws[i];
        } else if (ws[i] < 0x800) {
            utf8 += (char)(0xC0 | (ws[i] >> 6));
            utf8 += (char)(0x80 | (ws[i] & 0x3F));
        } else {
            utf8 += (char)(0xE0 | (ws[i] >> 12));
            utf8 += (char)(0x80 | ((ws[i] >> 6) & 0x3F));
            utf8 += (char)(0x80 | (ws[i] & 0x3F));
        }
    }
    return [NSString stringWithUTF8String:utf8.c_str()];
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Video extensions Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡
static bool IsVideoExt(const wchar_t* path) {
    if (!path) return false;
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    const wchar_t* ext = dot + 1;
    // Common video extensions
    return wcscasecmp(ext, L"mp4") == 0 || wcscasecmp(ext, L"mov") == 0 ||
           wcscasecmp(ext, L"m4v") == 0 || wcscasecmp(ext, L"avi") == 0 ||
           wcscasecmp(ext, L"mkv") == 0 || wcscasecmp(ext, L"webm") == 0 ||
           wcscasecmp(ext, L"wmv") == 0 || wcscasecmp(ext, L"flv") == 0 ||
           wcscasecmp(ext, L"mpg") == 0 || wcscasecmp(ext, L"mpeg") == 0 ||
           wcscasecmp(ext, L"ts") == 0 || wcscasecmp(ext, L"m2ts") == 0 ||
           wcscasecmp(ext, L"3gp") == 0 || wcscasecmp(ext, L"vob") == 0;
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Constructor / Destructor Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

TmMedia::TmMedia()
    : m_cs(nullptr), m_thread(nullptr), m_avAsset(nullptr), m_avReader(nullptr),
      m_cgInited(false), m_pixels(nullptr), m_w(0), m_h(0), m_stride(0),
      m_dirty(false), m_isVideo(false), m_playing(false), m_loop(false),
      m_scaleMode(0), m_threadRun(false)
{
    m_path[0] = L'\0';
    pthread_mutex_t* mtx = new pthread_mutex_t;
    pthread_mutex_init(mtx, nullptr);
    m_cs = mtx;
}

TmMedia::~TmMedia() {
    Shutdown();
    if (m_cs) {
        pthread_mutex_t* mtx = (pthread_mutex_t*)m_cs;
        pthread_mutex_destroy(mtx);
        delete mtx;
        m_cs = nullptr;
    }
}

void TmMedia::Init() {
    TM_INFO("TmMedia (AVFoundation) initialized");
}

void TmMedia::Shutdown() {
    Close();
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Open / Close Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

bool TmMedia::Open(const wchar_t* path, int scaleMode) {
    if (!path || !path[0]) return false;
    Close();

    wcscpy(m_path, path);
    m_scaleMode = scaleMode;

    if (IsVideoPath(path)) {
        return OpenVideo(path);
    } else {
        return LoadImage(path, scaleMode);
    }
}

void TmMedia::Close() {
    if (m_threadRun) {
        m_threadRun = false;
        m_playing = false;
        if (m_thread) {
            pthread_t* t = (pthread_t*)m_thread;
            pthread_join(*t, nullptr);
            delete t;
            m_thread = nullptr;
        }
    }

    if (m_avReader) {
        CFRelease((AVAssetReader*)m_avReader);
        m_avReader = nullptr;
    }
    if (m_avAsset) {
        CFRelease((AVAsset*)m_avAsset);
        m_avAsset = nullptr;
    }

    FreeFrame();
    m_isVideo = false;
    m_playing = false;
    m_dirty = false;
    m_path[0] = L'\0';
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Image loading (CoreGraphics) Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

bool TmMedia::LoadImage(const wchar_t* path, int scaleMode) {
    NSString* nsPath = WCharToNSString(path);
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    if (!url) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL((CFURLRef)url, nullptr);
    if (!src) {
        TM_WARN("Failed to create image source: %s", [nsPath UTF8String]);
        return false;
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!image) {
        TM_WARN("Failed to load image: %s", [nsPath UTF8String]);
        return false;
    }

    int origW = (int)CGImageGetWidth(image);
    int origH = (int)CGImageGetHeight(image);

    // Determine output dimensions based on scale mode
    // For images, we use the original size (or capped to 1920x1080)
    int outW = origW, outH = origH;
    if (outW > 1920) { outH = outH * 1920 / outW; outW = 1920; }
    if (outH > 1080) { outW = outW * 1080 / outH; outH = 1080; }

    AllocFrame(outW, outH);

    // Create a bitmap context to draw the image into our BGRA buffer
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        m_pixels, outW, outH, 8, m_stride,
        colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(colorSpace);

    if (!ctx) {
        TM_WARN("Failed to create bitmap context");
        CGImageRelease(image);
        return false;
    }

    // Flip Y (CG is bottom-up, we want top-down)
    CGContextTranslateCTM(ctx, 0, outH);
    CGContextScaleCTM(ctx, 1.0, -1.0);

    // Draw image to fill the frame
    CGContextDrawImage(ctx, CGRectMake(0, 0, outW, outH), image);
    CGContextRelease(ctx);
    CGImageRelease(image);

    m_dirty = true;
    m_isVideo = false;
    TM_INFO("Image loaded: %dx%d -> %dx%d", origW, origH, outW, outH);
    return true;
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Video loading (AVFoundation) Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

bool TmMedia::OpenVideo(const wchar_t* path) {
    NSString* nsPath = WCharToNSString(path);
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    if (!url) return false;

    AVAsset* asset = [AVAsset assetWithURL:url];
    if (!asset || [asset.tracks count] == 0) {
        TM_WARN("Failed to open video: %s", [nsPath UTF8String]);
        return false;
    }

    // Find the video track
    AVAssetTrack* videoTrack = nil;
    for (AVAssetTrack* track in asset.tracks) {
        if ([track.mediaType isEqualToString:AVMediaTypeVideo]) {
            videoTrack = track;
            break;
        }
    }
    if (!videoTrack) {
        TM_WARN("No video track found: %s", [nsPath UTF8String]);
        CFRelease(asset);
        return false;
    }

    // Get natural size
    CGSize naturalSize = [videoTrack naturalSize];
    int vidW = (int)naturalSize.width;
    int vidH = (int)naturalSize.height;

    // Cap to 1920x1080
    if (vidW > 1920) { vidH = vidH * 1920 / vidW; vidW = 1920; }
    if (vidH > 1080) { vidW = vidW * 1080 / vidH; vidH = 1080; }

    AllocFrame(vidW, vidH);

    m_avAsset = (void*)CFRetain(asset);
    m_isVideo = true;

    // Extract first frame
    ExtractFirstFrame();

    // Start decode thread
    m_threadRun = true;
    m_playing = true;
    pthread_t* t = new pthread_t;
    pthread_create(t, nullptr, VideoThread, this);
    m_thread = t;

    TM_INFO("Video opened: %dx%d -> %dx%d", (int)naturalSize.width, (int)naturalSize.height, vidW, vidH);
    return true;
}

void TmMedia::ExtractFirstFrame() {
    if (!m_avAsset) return;

    AVAsset* asset = (AVAsset*)m_avAsset;
    NSError* error = nil;

    // Create asset reader for a single frame
    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    if (!reader) return;

    AVAssetTrack* videoTrack = nil;
    for (AVAssetTrack* track in asset.tracks) {
        if ([track.mediaType isEqualToString:AVMediaTypeVideo]) {
            videoTrack = track;
            break;
        }
    }
    if (!videoTrack) {
        CFRelease(reader);
        return;
    }

    NSDictionary* settings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    AVAssetReaderTrackOutput* output =
        [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack
                                               outputSettings:settings];
    [reader addOutput:output];
    [reader startReading];

    CMSampleBufferRef sample = [output copyNextSampleBuffer];
    if (sample) {
        CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sample);
        if (pixelBuffer) {
            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            void* baseAddr = CVPixelBufferGetBaseAddress(pixelBuffer);
            int srcW = (int)CVPixelBufferGetWidth(pixelBuffer);
            int srcH = (int)CVPixelBufferGetHeight(pixelBuffer);
            int srcStride = (int)CVPixelBufferGetBytesPerRow(pixelBuffer);

            // Copy/resize to our frame buffer
            if (srcW == m_w && srcH == m_h && srcStride == m_stride) {
                memcpy(m_pixels, baseAddr, m_h * m_stride);
            } else {
                // Simple nearest-neighbor resize
                for (int y = 0; y < m_h; y++) {
                    int srcY = y * srcH / m_h;
                    BYTE* dst = m_pixels + y * m_stride;
                    BYTE* srcRow = (BYTE*)baseAddr + srcY * srcStride;
                    for (int x = 0; x < m_w; x++) {
                        int srcX = x * srcW / m_w;
                        memcpy(dst + x * 4, srcRow + srcX * 4, 4);
                    }
                }
            }

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            m_dirty = true;
        }
        CFRelease(sample);
    }

    CFRelease(reader);
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Video thread Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

void* TmMedia::VideoThread(void* pv) {
    TmMedia* self = (TmMedia*)pv;
    self->VideoLoop();
    return nullptr;
}

void TmMedia::VideoLoop() {
    AVAsset* asset = (AVAsset*)m_avAsset;

    while (m_threadRun) {
        if (!m_playing) {
            usleep(10000); // 10ms sleep when paused
            continue;
        }

        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) { usleep(100000); continue; }

        AVAssetTrack* videoTrack = nil;
        for (AVAssetTrack* track in asset.tracks) {
            if ([track.mediaType isEqualToString:AVMediaTypeVideo]) {
                videoTrack = track;
                break;
            }
        }
        if (!videoTrack) { CFRelease(reader); usleep(100000); continue; }

        NSDictionary* settings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        AVAssetReaderTrackOutput* output =
            [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack
                                               outputSettings:settings];
        [reader addOutput:output];
        [reader startReading];

        while (m_threadRun && m_playing && reader.status == AVAssetReaderStatusReading) {
            CMSampleBufferRef sample = [output copyNextSampleBuffer];
            if (!sample) break;

            CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sample);
            if (pixelBuffer) {
                CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
                void* baseAddr = CVPixelBufferGetBaseAddress(pixelBuffer);
                int srcW = (int)CVPixelBufferGetWidth(pixelBuffer);
                int srcH = (int)CVPixelBufferGetHeight(pixelBuffer);
                int srcStride = (int)CVPixelBufferGetBytesPerRow(pixelBuffer);

                pthread_mutex_lock((pthread_mutex_t*)m_cs);
                if (srcW == m_w && srcH == m_h && srcStride == m_stride) {
                    memcpy(m_pixels, baseAddr, m_h * m_stride);
                } else {
                    for (int y = 0; y < m_h; y++) {
                        int srcY = y * srcH / m_h;
                        BYTE* dst = m_pixels + y * m_stride;
                        BYTE* srcRow = (BYTE*)baseAddr + srcY * srcStride;
                        for (int x = 0; x < m_w; x++) {
                            int srcX = x * srcW / m_w;
                            memcpy(dst + x * 4, srcRow + srcX * 4, 4);
                        }
                    }
                }
                m_dirty = true;
                pthread_mutex_unlock((pthread_mutex_t*)m_cs);

                CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            }
            CFRelease(sample);

            // Frame rate control (~30fps)
            usleep(33000);
        }

        CFRelease(reader);

        // Loop or stop
        if (m_threadRun && m_playing) {
            if (!m_loop) {
                m_playing = false;
                break;
            }
            // Restart from beginning
        }
    }
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Playback control Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

void TmMedia::Play() { m_playing = true; }
void TmMedia::Pause() { m_playing = false; }
void TmMedia::Stop() { m_playing = false; }

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Frame access Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

bool TmMedia::BeginFrame(const void** outPixels, int* w, int* h, int* stride) {
    if (!m_pixels) return false;
    pthread_mutex_lock((pthread_mutex_t*)m_cs);
    *outPixels = m_pixels;
    if (w) *w = m_w;
    if (h) *h = m_h;
    if (stride) *stride = m_stride;
    return true;
}

void TmMedia::EndFrame() {
    pthread_mutex_unlock((pthread_mutex_t*)m_cs);
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Static helpers Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

bool TmMedia::IsVideoPath(const wchar_t* path) {
    return IsVideoExt(path);
}

BYTE* TmMedia::ExtractFirstFrame(const wchar_t* path, int* outW, int* outH, int* outStride) {
    if (!path || !IsVideoPath(path)) return nullptr;

    NSString* nsPath = WCharToNSString(path);
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    if (!url) return nullptr;

    AVAsset* asset = [AVAsset assetWithURL:url];
    if (!asset) return nullptr;

    AVAssetTrack* videoTrack = nil;
    for (AVAssetTrack* track in asset.tracks) {
        if ([track.mediaType isEqualToString:AVMediaTypeVideo]) {
            videoTrack = track;
            break;
        }
    }
    if (!videoTrack) { CFRelease(asset); return nullptr; }

    NSError* error = nil;
    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    if (!reader) { CFRelease(asset); return nullptr; }

    NSDictionary* settings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    AVAssetReaderTrackOutput* output =
        [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack
                                               outputSettings:settings];
    [reader addOutput:output];
    [reader startReading];

    CMSampleBufferRef sample = [output copyNextSampleBuffer];
    BYTE* result = nullptr;
    if (sample) {
        CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sample);
        if (pixelBuffer) {
            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            int w = (int)CVPixelBufferGetWidth(pixelBuffer);
            int h = (int)CVPixelBufferGetHeight(pixelBuffer);
            int stride = (int)CVPixelBufferGetBytesPerRow(pixelBuffer);

            // Cap to 320x180 for thumbnails
            int thumbW = w, thumbH = h;
            if (thumbW > 320) { thumbH = thumbH * 320 / thumbW; thumbW = 320; }
            if (thumbH > 180) { thumbW = thumbW * 180 / thumbH; thumbH = 180; }
            int thumbStride = thumbW * 4;

            result = (BYTE*)malloc(thumbH * thumbStride);
            void* baseAddr = CVPixelBufferGetBaseAddress(pixelBuffer);

            for (int y = 0; y < thumbH; y++) {
                int srcY = y * h / thumbH;
                BYTE* dst = result + y * thumbStride;
                BYTE* srcRow = (BYTE*)baseAddr + srcY * stride;
                for (int x = 0; x < thumbW; x++) {
                    int srcX = x * w / thumbW;
                    memcpy(dst + x * 4, srcRow + srcX * 4, 4);
                }
            }

            *outW = thumbW;
            *outH = thumbH;
            *outStride = thumbStride;

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        }
        CFRelease(sample);
    }

    CFRelease(reader);
    CFRelease(asset);
    return result;
}

// Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡ Frame buffer management Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡Î“Ã¶Ã‡

void TmMedia::AllocFrame(int w, int h) {
    FreeFrame();
    m_w = w;
    m_h = h;
    m_stride = w * 4;
    m_pixels = (BYTE*)malloc(h * m_stride);
    memset(m_pixels, 0, h * m_stride);
}

void TmMedia::FreeFrame() {
    if (m_pixels) {
        free(m_pixels);
        m_pixels = nullptr;
    }
    m_w = m_h = m_stride = 0;
    m_dirty = false;
}

#endif // VDJ_MAC
