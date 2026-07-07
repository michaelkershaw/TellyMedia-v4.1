#pragma once
#include "tm/TmPlatform.h"

#if defined(VDJ_WIN)
// Forward declaration avoids pulling Media Foundation headers.
struct IMFSourceReader;
#elif defined(VDJ_MAC)
// macOS uses AVFoundation (forward declared in .mm implementation)
#endif

// Loads images and decodes video into a top-down BGRA frame buffer.
// Windows: GDI+ for images, Media Foundation for video.
// macOS: CoreGraphics for images, AVFoundation for video.
class TmMedia {
public:
    TmMedia();
    ~TmMedia();

    void Init();
    void Shutdown();

    // Load any supported file. Returns true if a frame is (or will be) available.
    bool Open(const wchar_t* path, int scaleMode);
    void Close();

    void Play();
    void Pause();
    void Stop();
    void SetLooping(bool loop) { m_loop = loop; }

    bool IsVideo() const { return m_isVideo; }
    bool IsPlaying() const { return m_playing; }

    // Copy the latest frame into caller-provided buffer info. Returns false if none.
    bool BeginFrame(const void** outPixels, int* w, int* h, int* stride);
    void EndFrame();
    bool HasNewFrame() const { return m_dirty; }

    static bool IsVideoPath(const wchar_t* path);
    // Extract the first frame as a BGRA buffer (caller frees with free()).
    static BYTE* ExtractFirstFrame(const wchar_t* path, int* outW, int* outH, int* outStride);

private:
    void AllocFrame(int w, int h);
    void FreeFrame();
    bool LoadImage(const wchar_t* path, int scaleMode);
    bool OpenVideo(const wchar_t* path);
#if defined(VDJ_WIN)
    static DWORD WINAPI VideoThread(LPVOID pv);
#elif defined(VDJ_MAC)
    static void* VideoThread(void* pv);
#endif
    void VideoLoop();

#if defined(VDJ_WIN)
    CRITICAL_SECTION m_cs;
    HANDLE           m_thread;
    IMFSourceReader* m_reader;
    ULONG_PTR        m_gdiplusToken;
    bool             m_mfStarted;
#elif defined(VDJ_MAC)
    void*            m_cs;       // pthread_mutex_t*
    void*            m_thread;   // pthread_t*
    void*            m_avAsset;  // AVAsset*
    void*            m_avReader; // AVAssetReader*
    bool             m_cgInited;
#endif
    BYTE*            m_pixels;   // BGRA top-down
    int              m_w, m_h, m_stride;
    volatile bool    m_dirty;

    bool             m_isVideo;
    volatile bool    m_playing;
    bool             m_loop;
    int              m_scaleMode;
    volatile bool    m_threadRun;
    wchar_t          m_path[MAX_PATH];
};
