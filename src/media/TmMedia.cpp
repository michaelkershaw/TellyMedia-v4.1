#include "tm/TmMedia.h"
#include "tm/TmLogger.h"
#include "tm/TmTypes.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <gdiplus.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "gdiplus.lib")

TmMedia::TmMedia()
    : m_pixels(nullptr), m_w(0), m_h(0), m_stride(0), m_dirty(false),
      m_isVideo(false), m_playing(false), m_loop(true), m_scaleMode(SCALE_ASPECT),
      m_reader(nullptr), m_thread(nullptr), m_threadRun(false),
      m_gdiplusToken(0), m_mfStarted(false)
{
    m_path[0] = 0;
    InitializeCriticalSection(&m_cs);
}

TmMedia::~TmMedia()
{
    Shutdown();
    DeleteCriticalSection(&m_cs);
}

void TmMedia::Init()
{
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gsi, nullptr);
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) m_mfStarted = true;
    TM_INFO("TmMedia init (gdi+=%llu mf=%d)", (unsigned long long)m_gdiplusToken, m_mfStarted);
}

void TmMedia::Shutdown()
{
    Close();
    if (m_mfStarted) { MFShutdown(); m_mfStarted = false; }
    if (m_gdiplusToken) { Gdiplus::GdiplusShutdown(m_gdiplusToken); m_gdiplusToken = 0; }
}

bool TmMedia::IsVideoPath(const wchar_t* path)
{
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    const wchar_t* ext = dot + 1;
    static const wchar_t* v[] = {
        L"mp4", L"avi", L"mov", L"wmv", L"mkv", L"flv", L"m4v", L"mpg",
        L"mpeg", L"ts", L"3gp", L"webm", L"m2ts", L"mts", L"vob", L"ogv", L"asf", nullptr
    };
    for (int i = 0; v[i]; ++i) if (_wcsicmp(ext, v[i]) == 0) return true;
    return false;
}

BYTE* TmMedia::ExtractFirstFrame(const wchar_t* path, int* outW, int* outH, int* outStride)
{
    if (!outW || !outH || !outStride) return nullptr;
    *outW = *outH = *outStride = 0;

    // Quick MF startup/shutdown for thumbnail extraction (no persistent state).
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    IMFAttributes* attr = nullptr;
    MFCreateAttributes(&attr, 1);
    if (attr) attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    IMFSourceReader* reader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(path, attr, &reader);
    if (attr) attr->Release();
    if (FAILED(hr)) { MFShutdown(); return nullptr; }

    IMFMediaType* mt = nullptr;
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt);
    mt->Release();
    if (FAILED(hr)) { reader->Release(); MFShutdown(); return nullptr; }

    IMFMediaType* cur = nullptr;
    UINT32 w = 0, h = 0;
    if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
        MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
        cur->Release();
    }
    if (w == 0 || h == 0) { reader->Release(); MFShutdown(); return nullptr; }

    // Read the first sample.
    DWORD streamIndex = 0, flags = 0;
    LONGLONG ts = 0;
    IMFSample* sample = nullptr;
    hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &ts, &sample);
    reader->Release();
    MFShutdown();

    if (FAILED(hr) || !sample) return nullptr;

    BYTE* pixels = nullptr;
    IMFMediaBuffer* buf = nullptr;
    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
        BYTE* data = nullptr; DWORD cur = 0, maxLen = 0;
        if (SUCCEEDED(buf->Lock(&data, &maxLen, &cur))) {
            int stride = w * 4;
            pixels = (BYTE*)malloc((size_t)stride * h);
            if (pixels) {
                // Copy directly (MF RGB32 is already top-down)
                for (int y = 0; y < h; ++y) {
                    const BYTE* s = data + (size_t)y * stride;
                    BYTE* d = pixels + (size_t)y * stride;
                    for (int x = 0; x < w; ++x) {
                        d[x*4+0] = s[x*4+0];  // B
                        d[x*4+1] = s[x*4+1];  // G
                        d[x*4+2] = s[x*4+2];  // R
                        d[x*4+3] = 255;       // A (force opaque)
                    }
                }
                *outW = w; *outH = h; *outStride = stride;
            }
            buf->Unlock();
        }
        buf->Release();
    }
    sample->Release();
    return pixels;
}

void TmMedia::AllocFrame(int w, int h)
{
    if (m_pixels && m_w == w && m_h == h) return;
    FreeFrame();
    m_w = w; m_h = h; m_stride = w * 4;
    m_pixels = (BYTE*)malloc((size_t)m_stride * h);
    if (m_pixels) memset(m_pixels, 0, (size_t)m_stride * h);
}

void TmMedia::FreeFrame()
{
    if (m_pixels) { free(m_pixels); m_pixels = nullptr; }
    m_w = m_h = m_stride = 0;
}

bool TmMedia::Open(const wchar_t* path, int scaleMode)
{
    Close();
    m_scaleMode = scaleMode;
    wcscpy_s(m_path, path);
    m_isVideo = IsVideoPath(path);
    if (m_isVideo) return OpenVideo(path);
    return LoadImage(path, scaleMode);
}

bool TmMedia::LoadImage(const wchar_t* path, int /*scaleMode*/)
{
    Gdiplus::Bitmap bmp(path);
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        TM_WARN("LoadImage failed: %S", path);
        return false;
    }
    int w = (int)bmp.GetWidth();
    int h = (int)bmp.GetHeight();
    if (w <= 0 || h <= 0) return false;

    EnterCriticalSection(&m_cs);
    AllocFrame(w, h);
    if (m_pixels) {
        Gdiplus::BitmapData bd = {};
        Gdiplus::Rect rc(0, 0, w, h);
        if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
            const BYTE* src = (const BYTE*)bd.Scan0;
            for (int y = 0; y < h; ++y)
                memcpy(m_pixels + y * m_stride, src + y * bd.Stride, w * 4);
            bmp.UnlockBits(&bd);
        }
        m_dirty = true;
    }
    LeaveCriticalSection(&m_cs);
    m_playing = false;
    TM_INFO("Image loaded %dx%d: %S", w, h, path);
    return m_pixels != nullptr;
}

bool TmMedia::OpenVideo(const wchar_t* path)
{
    if (!m_mfStarted) return false;

    IMFAttributes* attr = nullptr;
    MFCreateAttributes(&attr, 1);
    if (attr) attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    HRESULT hr = MFCreateSourceReaderFromURL(path, attr, &m_reader);
    if (attr) attr->Release();
    if (FAILED(hr)) { TM_HR("MFCreateSourceReaderFromURL", hr); return false; }

    IMFMediaType* mt = nullptr;
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = m_reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt);
    mt->Release();
    if (FAILED(hr)) { TM_HR("SetCurrentMediaType RGB32", hr); Close(); return false; }

    IMFMediaType* cur = nullptr;
    if (SUCCEEDED(m_reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
        cur->Release();
        if (w && h) { EnterCriticalSection(&m_cs); AllocFrame((int)w, (int)h); LeaveCriticalSection(&m_cs); }
    }

    m_threadRun = true;
    m_playing = true;
    m_thread = CreateThread(nullptr, 0, VideoThread, this, 0, nullptr);
    TM_INFO("Video opened: %S", path);
    return true;
}

DWORD WINAPI TmMedia::VideoThread(LPVOID pv)
{
    ((TmMedia*)pv)->VideoLoop();
    return 0;
}

void TmMedia::VideoLoop()
{
    // Presentation clock: rebased on first frame, on loop, and after pause.
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    bool      haveClock = false;
    LONGLONG  baseTs = 0;             // sample timestamp at clock origin (100ns)
    LARGE_INTEGER baseQpc = {};       // wall-clock counter at origin
    bool      wasPaused = false;

    while (m_threadRun) {
        if (!m_playing) { wasPaused = true; Sleep(15); continue; }
        if (wasPaused) { haveClock = false; wasPaused = false; }  // rebase after resume

        DWORD streamIndex = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = m_reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                          0, &streamIndex, &flags, &ts, &sample);
        if (FAILED(hr)) { TM_HR("ReadSample", hr); break; }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (m_loop) {
                PROPVARIANT pos; PropVariantInit(&pos);
                pos.vt = VT_I8; pos.hVal.QuadPart = 0;
                m_reader->SetCurrentPosition(GUID_NULL, pos);
                PropVariantClear(&pos);
                haveClock = false;   // restart presentation clock
                if (sample) sample->Release();
                continue;
            } else {
                m_playing = false;
                if (sample) sample->Release();
                continue;
            }
        }

        if (sample) {
            // Pace to the sample's presentation time so the video plays at
            // its real frame rate instead of as fast as it can decode.
            if (!haveClock) {
                baseTs = ts;
                QueryPerformanceCounter(&baseQpc);
                haveClock = true;
            } else {
                double targetMs = (double)(ts - baseTs) / 10000.0;
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                double elapsedMs = (double)(now.QuadPart - baseQpc.QuadPart) * 1000.0 / (double)freq.QuadPart;
                double waitMs = targetMs - elapsedMs;
                // Cap the wait so a seek/pause glitch can't stall the thread.
                while (waitMs > 1.0 && m_threadRun && m_playing) {
                    Sleep(waitMs > 30.0 ? 15 : 1);
                    QueryPerformanceCounter(&now);
                    elapsedMs = (double)(now.QuadPart - baseQpc.QuadPart) * 1000.0 / (double)freq.QuadPart;
                    waitMs = targetMs - elapsedMs;
                }
            }

            IMFMediaBuffer* buf = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
                BYTE* data = nullptr; DWORD cur = 0, maxLen = 0;
                if (SUCCEEDED(buf->Lock(&data, &maxLen, &cur))) {
                    EnterCriticalSection(&m_cs);
                    if (m_pixels && m_w > 0 && m_h > 0) {
                        int rowBytes = m_w * 4;
                        // Copy directly (MF RGB32 is already top-down)
                        for (int y = 0; y < m_h; ++y) {
                            const BYTE* s = data + (size_t)y * rowBytes;
                            BYTE* d = m_pixels + (size_t)y * m_stride;
                            for (int x = 0; x < m_w; ++x) {
                                d[x*4+0] = s[x*4+0];  // B
                                d[x*4+1] = s[x*4+1];  // G
                                d[x*4+2] = s[x*4+2];  // R
                                d[x*4+3] = 255;       // A (force opaque)
                            }
                        }
                        m_dirty = true;
                    }
                    LeaveCriticalSection(&m_cs);
                    buf->Unlock();
                }
                buf->Release();
            }
            sample->Release();
        }
    }
}

void TmMedia::Play()  { m_playing = true; }
void TmMedia::Pause() { m_playing = false; }
void TmMedia::Stop()  { m_playing = false; }

bool TmMedia::BeginFrame(const void** outPixels, int* w, int* h, int* stride)
{
    EnterCriticalSection(&m_cs);
    if (!m_pixels) { LeaveCriticalSection(&m_cs); return false; }
    *outPixels = m_pixels; *w = m_w; *h = m_h; *stride = m_stride;
    return true;
}

void TmMedia::EndFrame()
{
    m_dirty = false;
    LeaveCriticalSection(&m_cs);
}

void TmMedia::Close()
{
    m_threadRun = false;
    m_playing = false;
    if (m_thread) { WaitForSingleObject(m_thread, 2000); CloseHandle(m_thread); m_thread = nullptr; }
    if (m_reader) { m_reader->Release(); m_reader = nullptr; }
    EnterCriticalSection(&m_cs);
    FreeFrame();
    m_dirty = false;
    LeaveCriticalSection(&m_cs);
    m_isVideo = false;
    m_path[0] = 0;
}
