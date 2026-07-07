#include "tm/TmPlugin.h"
#include "tm/TmLogger.h"
#include <gdiplus.h>
#include <algorithm>
#include <vector>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

ULONG_PTR TmPlugin::s_gdiToken = 0;
int TmPlugin::s_gdiRef = 0;

static void GdiplusAddRef() {
    if (TmPlugin::s_gdiRef++ == 0) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&TmPlugin::s_gdiToken, &si, nullptr);
    }
}
static void GdiplusRelease() {
    if (--TmPlugin::s_gdiRef == 0 && TmPlugin::s_gdiToken) {
        Gdiplus::GdiplusShutdown(TmPlugin::s_gdiToken);
        TmPlugin::s_gdiToken = 0;
    }
}

static void FetchNowPlayingText(TmPlugin* self, wchar_t* outText, int outTextMax)
{
    if (self) self->HostGetNowPlayingText(outText, outTextMax);
}

static Gdiplus::Color HSVtoRGB(float h, float s, float v)
{
    float r = 0, g = 0, b = 0;
    if (s == 0) {
        r = g = b = v;
    } else {
        int i = (int)(h * 6);
        float f = (h * 6) - i;
        float p = v * (1 - s);
        float q = v * (1 - s * f);
        float t = v * (1 - s * (1 - f));
        switch (i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            case 5: r = v; g = p; b = q; break;
        }
    }
    return Gdiplus::Color(255, (BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255));
}

static Gdiplus::Color ColorFromRgbAndAlpha(COLORREF rgb, BYTE alpha)
{
    return Gdiplus::Color(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
}

static bool DetectKaraokeTrack(const wchar_t* filePath)
{
    if (!filePath || !filePath[0]) return false;

    wchar_t lowerPath[MAX_PATH];
    wcscpy_s(lowerPath, filePath);
    _wcslwr_s(lowerPath);

    const wchar_t* ext = wcsrchr(lowerPath, L'.');
    if (ext && wcscmp(ext, L".cdg") == 0) return true;

    if (ext && (wcscmp(ext, L".zip") == 0 || wcscmp(ext, L".rar") == 0)) {
        if (wcsstr(lowerPath, L"karaoke") != nullptr) return true;
    }

    if (wcsstr(lowerPath, L"karaoke") != nullptr) return true;

    return false;
}

static void DrawTextWithOptionalGradient(Gdiplus::Graphics& graphics,
                                         const wchar_t* text,
                                         const Gdiplus::Font& font,
                                         const Gdiplus::RectF& rect,
                                         const Gdiplus::StringFormat& format,
                                         const Gdiplus::Color& color1,
                                         const Gdiplus::Color& color2)
{
    if (!text || !text[0]) return;

    if (color1.GetValue() == color2.GetValue()) {
        Gdiplus::SolidBrush brush(color1);
        graphics.DrawString(text, -1, &font, rect, &format, &brush);
        return;
    }

    Gdiplus::LinearGradientBrush brush(rect, color1, color2, Gdiplus::LinearGradientModeHorizontal);
    graphics.DrawString(text, -1, &font, rect, &format, &brush);
}

static void DrawTextOutlinePasses(Gdiplus::Graphics& graphics,
                                  const wchar_t* text,
                                  const Gdiplus::Font& font,
                                  const Gdiplus::RectF& rect,
                                  const Gdiplus::StringFormat& format,
                                  const Gdiplus::Color& outlineColor,
                                  int outlineWidth)
{
    if (!text || !text[0] || outlineWidth <= 0) return;

    Gdiplus::SolidBrush outlineBrush(outlineColor);
    for (int r = 1; r <= outlineWidth; ++r) {
        const Gdiplus::REAL o = (Gdiplus::REAL)r;
        const Gdiplus::RectF offsets[8] = {
            Gdiplus::RectF(rect.X - o, rect.Y, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X + o, rect.Y, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X, rect.Y - o, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X, rect.Y + o, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X - o, rect.Y - o, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X - o, rect.Y + o, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X + o, rect.Y - o, rect.Width, rect.Height),
            Gdiplus::RectF(rect.X + o, rect.Y + o, rect.Width, rect.Height)
        };

        for (const auto& offsetRect : offsets) {
            graphics.DrawString(text, -1, &font, offsetRect, &format, &outlineBrush);
        }
    }
}

static void DrawAnimatedTextPanel(Gdiplus::Graphics& graphics,
                                  const OverlayPanel& p,
                                  const Gdiplus::RectF& panelRect,
                                  const Gdiplus::Color& textColor1,
                                  const Gdiplus::Color& textColor2)
{
    if (!p.textContent[0]) return;

    Gdiplus::GraphicsState state = graphics.Save();
    graphics.SetClip(Gdiplus::Rect((INT)panelRect.X, (INT)panelRect.Y, (INT)panelRect.Width, (INT)panelRect.Height));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    double t = (double)GetTickCount64() * 0.001;
    const wchar_t* renderText = p.textContent;
    wchar_t typeBuf[512] = {};

    if (p.textAnimMode == 5) {
        size_t len = wcslen(p.textContent);
        if (len > 0) {
            int stepCount = (int)len + 8;
            int step = (int)fmod(t * 14.0, (double)stepCount);
            size_t count = (size_t)max(0, min(step, (int)len));
            wcsncpy_s(typeBuf, _countof(typeBuf), p.textContent, count);
            typeBuf[count] = L'\0';
            if (count < len && ((int)(t * 2.0) % 2) == 0) {
                wcscat_s(typeBuf, _countof(typeBuf), L"|");
            }
            renderText = typeBuf;
        }
    }

    Gdiplus::FontFamily fontFamily(p.fontName[0] ? p.fontName : L"Arial");
    Gdiplus::REAL baseSize = (Gdiplus::REAL)max(6, p.fontSize);
    Gdiplus::REAL drawSize = baseSize;
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::RectF drawRect = panelRect;

    if (p.textAnimMode == 1 || p.textAnimMode == 2 || p.textAnimMode == 3) {
        Gdiplus::Font measureFont(&fontFamily, baseSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF measureLayout(0.0f, 0.0f, 100000.0f, 100000.0f);
        Gdiplus::RectF textBounds;
        graphics.MeasureString(renderText, -1, &measureFont, measureLayout, &textBounds);
        float textW = max(1.0f, textBounds.Width);
        float textH = max(1.0f, textBounds.Height);
        float speed = p.scrollSpeed > 0 ? p.scrollSpeed : 180.0f;

        if (p.textAnimMode == 1 || p.textAnimMode == 2) {
            format.SetAlignment(Gdiplus::StringAlignmentNear);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            format.SetTrimming(Gdiplus::StringTrimmingNone);
            float travel = panelRect.Width + textW;
            float duration = travel / speed;
            if (duration < 0.001f) duration = 0.001f;
            float prog = (float)fmod(t, duration) / duration;
            float x = (p.textAnimMode == 1)
                ? (panelRect.X + panelRect.Width - prog * travel)
                : (panelRect.X - textW + prog * travel);
            drawRect = Gdiplus::RectF(x, panelRect.Y, textW + 8.0f, panelRect.Height);
        } else if (p.textAnimMode == 3) {
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            float maxX = max(0.0f, (panelRect.Width - textW) * 0.6f);
            float maxY = max(0.0f, (panelRect.Height - textH) * 0.5f);
            float phase = (float)fmod(t * max(0.02f, speed / 140.0f), 1.0);
            float tri = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
            float yWave = 0.5f + 0.5f * sinf((float)t * 1.1f);
            drawRect = Gdiplus::RectF(panelRect.X + maxX * tri, panelRect.Y + maxY * yWave, panelRect.Width, panelRect.Height);
        }
    } else if (p.textAnimMode == 4) {
        float pulse = 0.88f + 0.18f * (0.5f + 0.5f * sinf((float)t * 2.0f));
        drawSize = baseSize * pulse;
    }

    Gdiplus::Font drawFont(&fontFamily, drawSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    if (p.outlineWidth > 0) {
        DrawTextOutlinePasses(graphics, renderText, drawFont, drawRect, format,
            Gdiplus::Color(255, GetRValue(p.outlineColor), GetGValue(p.outlineColor), GetBValue(p.outlineColor)),
            p.outlineWidth);
    }

    DrawTextWithOptionalGradient(graphics, renderText, drawFont, drawRect, format, textColor1, textColor2);
    graphics.Restore(state);
}

static bool CreateTextureFromImage(ID3D11Device* device, Gdiplus::Image* source, ID3D11ShaderResourceView** outSrv)
{
    if (!device || !source || !outSrv) return false;
    *outSrv = nullptr;

    UINT w = source->GetWidth();
    UINT h = source->GetHeight();
    if (w == 0 || h == 0) return false;

    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics gfx(&bmp);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.DrawImage(source, 0, 0, w, h);

    Gdiplus::Rect rc(0, 0, (INT)w, (INT)h);
    Gdiplus::BitmapData bd = {};
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        return false;
    }

    std::vector<BYTE> pixels((size_t)w * h * 4);
    const BYTE* scan0 = (const BYTE*)bd.Scan0;
    int stride = bd.Stride;
    if (stride < 0) {
        scan0 += (size_t)(h - 1) * (size_t)(-stride);
        stride = -stride;
    }
    for (UINT y = 0; y < h; ++y) {
        memcpy(&pixels[(size_t)y * w * 4], scan0 + (size_t)y * stride, (size_t)w * 4);
    }
    bmp.UnlockBits(&bd);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = pixels.data();
    init.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&td, &init, &tex);
    if (FAILED(hr) || !tex) return false;

    hr = device->CreateShaderResourceView(tex, nullptr, outSrv);
    tex->Release();
    return SUCCEEDED(hr) && *outSrv != nullptr;
}

static bool BuildScaledImagePixels(Gdiplus::Image* source, int dstW, int dstH, std::vector<BYTE>& outPixels)
{
    if (!source || dstW <= 0 || dstH <= 0) return false;

    Gdiplus::Bitmap bmp(dstW, dstH, PixelFormat32bppARGB);
    Gdiplus::Graphics gfx(&bmp);
    gfx.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    gfx.DrawImage(source, 0, 0, dstW, dstH);

    Gdiplus::Rect rc(0, 0, dstW, dstH);
    Gdiplus::BitmapData bd = {};
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        return false;
    }

    outPixels.resize((size_t)dstW * dstH * 4);
    const BYTE* scan0 = (const BYTE*)bd.Scan0;
    int stride = bd.Stride;
    if (stride < 0) {
        scan0 += (size_t)(dstH - 1) * (size_t)(-stride);
        stride = -stride;
    }
    for (int y = 0; y < dstH; ++y) {
        memcpy(&outPixels[(size_t)y * dstW * 4], scan0 + (size_t)y * stride, (size_t)dstW * 4);
    }
    bmp.UnlockBits(&bd);
    return true;
}

static inline BYTE BlendChannel(BYTE src, BYTE dst, BYTE srcA)
{
    const int invA = 255 - srcA;
    return (BYTE)((src * srcA + dst * invA + 127) / 255);
}

TmPlugin::TmPlugin()
    : m_settingsWnd(nullptr),
      m_d3dDevice(nullptr), m_d3dContext(nullptr), m_d3dReady(false),
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
    ZeroMemory(m_overlayImageSRVs, sizeof(m_overlayImageSRVs));
    ZeroMemory(m_overlayImagePixelW, sizeof(m_overlayImagePixelW));
    ZeroMemory(m_overlayImagePixelH, sizeof(m_overlayImagePixelH));
    
    // Initialize enhanced now playing panel defaults
    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) {
        m_overlayPanels[i].fontWeight = 1; // Bold
        m_overlayPanels[i].fontStyle = 0; // Normal
        m_overlayPanels[i].bgStyle = 0; // Solid
        m_overlayPanels[i].bgColor2 = RGB(0, 0, 0);
        m_overlayPanels[i].cornerRadius = 0;
        m_overlayPanels[i].shadowBlur = 0;
        m_overlayPanels[i].shadowColor = RGB(0, 0, 0);
        m_overlayPanels[i].layoutMode = 1; // Detailed
        wcscpy_s(m_overlayPanels[i].fontName, L"Segoe UI");
        m_overlayPanels[i].textColor = RGB(255, 255, 255);
        m_overlayPanels[i].textColor2 = m_overlayPanels[i].textColor;
        wcscpy_s(m_overlayPanels[i].headingFontName, L"Segoe UI");
        m_overlayPanels[i].headingTextColor = RGB(255, 255, 255);
        m_overlayPanels[i].headingTextColor2 = m_overlayPanels[i].headingTextColor;
        wcscpy_s(m_overlayPanels[i].songFontName, L"Segoe UI");
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
        m_overlayPanels[i].scrollSpeed = 300.0f; // Default scroll speed
        m_overlayPanels[i].outlineColor = RGB(0, 0, 0); // Default black outline
        m_overlayPanels[i].outlineWidth = 0; // Default no outline
        m_overlayPanels[i].textAnimMode = 0;
    }
    m_slideshowFrameW = 0;
    m_slideshowFrameH = 0;
    m_slideshowFrameValid = false;
    m_slideshowPrevW = 0;
    m_slideshowPrevH = 0;
    m_slideshowPrevValid = false;
    m_transitionProgress = 1.0f;
    ZeroMemory(&m_overlayLock, sizeof(m_overlayLock));
    InitializeCriticalSection(&m_overlayLock);
    m_overlayLockInit = true;

    // Initialize audio analysis
    ZeroMemory(&m_audio, sizeof(m_audio));
    m_audio.sampleRate = 44100;
    ZeroMemory(&m_audioLock, sizeof(m_audioLock));
    InitializeCriticalSection(&m_audioLock);
    m_audioLockInit = true;

    // Initialize shader settings
    m_shadersEnabled = false;
    m_shaderDisableOnKaraoke = true;
    m_karaokeAutoHide = true;
    m_mainShaderIndex = 0;
    m_isKaraoke = false;
    m_isAudioOnly = false;
    m_karaokeCheckFrame = 0;
    m_audioOnlyCheckFrame = 0;

    TmLicense::Init(&m_license);

    GdiplusAddRef();
    m_media.Init();
}

void TmPlugin::RefreshNowPlayingCache()
{
    m_songTitle[0] = L'\0';
    m_songArtist[0] = L'\0';
    m_songDisplay[0] = L'\0';
    m_currentPath[0] = L'\0';

    char titleA[256] = {};
    char artistA[256] = {};
    char pathA[MAX_PATH] = {};

    if (SUCCEEDED(GetStringInfo("get_title", titleA, sizeof(titleA))) && titleA[0]) {
        MultiByteToWideChar(CP_UTF8, 0, titleA, -1, m_songTitle, _countof(m_songTitle));
    }
    if (SUCCEEDED(GetStringInfo("get_artist", artistA, sizeof(artistA))) && artistA[0]) {
        MultiByteToWideChar(CP_UTF8, 0, artistA, -1, m_songArtist, _countof(m_songArtist));
    }
    if (SUCCEEDED(GetStringInfo("get_filepath", pathA, sizeof(pathA))) && pathA[0]) {
        MultiByteToWideChar(CP_UTF8, 0, pathA, -1, m_currentPath, _countof(m_currentPath));
    }

    if (m_songArtist[0] && m_songTitle[0]) {
        swprintf_s(m_songDisplay, L"%s - %s", m_songArtist, m_songTitle);
    } else if (m_songTitle[0]) {
        wcscpy_s(m_songDisplay, m_songTitle);
    } else if (m_songArtist[0]) {
        wcscpy_s(m_songDisplay, m_songArtist);
    } else if (m_currentPath[0]) {
        const wchar_t* base = wcsrchr(m_currentPath, L'\\');
        wcscpy_s(m_songDisplay, base ? base + 1 : m_currentPath);
    } else {
        wcscpy_s(m_songDisplay, L"No Track Playing");
    }
}

void TmPlugin::ClearOverlayImageCache()
{
    EnterCriticalSection(&m_overlayLock);
    ReleaseOverlayPanelTextures();
    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) {
        if (m_overlayImages[i]) {
            delete m_overlayImages[i];
            m_overlayImages[i] = nullptr;
        }
        m_overlayImagePixels[i].clear();
        m_overlayImagePixels[i].shrink_to_fit();
        m_overlayImagePixelW[i] = 0;
        m_overlayImagePixelH[i] = 0;
    }
    LeaveCriticalSection(&m_overlayLock);
}

void TmPlugin::ReleaseOverlayPanelTextures()
{
    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) {
        if (m_overlayImageSRVs[i]) {
            m_overlayImageSRVs[i]->Release();
            m_overlayImageSRVs[i] = nullptr;
        }
    }
}

void TmPlugin::BuildOverlayPanelTextures()
{
    EnterCriticalSection(&m_overlayLock);
    ReleaseOverlayPanelTextures();
    if (!m_d3dDevice) {
        LeaveCriticalSection(&m_overlayLock);
        return;
    }

    // All panels now use CPU overlay buffer rendering
    TM_INFO("BuildOverlayPanelTextures: using CPU overlay buffer for all panels");
    LeaveCriticalSection(&m_overlayLock);
}

void TmPlugin::DrawOverlayImagePanels()
{
    EnterCriticalSection(&m_overlayLock);
    int sorted[MAX_OVERLAY_PANELS];
    for (int i = 0; i < m_numOverlayPanels; ++i) sorted[i] = i;
    for (int i = 0; i < m_numOverlayPanels - 1; ++i)
        for (int j = i + 1; j < m_numOverlayPanels; ++j)
            if (m_overlayPanels[sorted[i]].zOrder > m_overlayPanels[sorted[j]].zOrder)
                { int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }

    for (int i = 0; i < m_numOverlayPanels; ++i) {
        int idx = sorted[i];
        const OverlayPanel& p = m_overlayPanels[idx];
        if (!p.visible || p.panelType != 1) continue;
        ID3D11ShaderResourceView* srv = m_overlayImageSRVs[idx];
        if (!srv) continue;
        m_renderer.DrawRectWithTexture(p.x, p.y, p.w, p.h, 1.0f, srv);
    }
    LeaveCriticalSection(&m_overlayLock);
}

float TmPlugin::EstimateOverlayImageLoad() const
{
    if (width <= 0 || height <= 0) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < m_numOverlayPanels; ++i) {
        const OverlayPanel& p = m_overlayPanels[i];
        if (!p.visible || p.panelType != 1) continue;
        total += max(0.0f, p.w) * max(0.0f, p.h);
    }
    return total;
}

TmPlugin::~TmPlugin()
{
    if (m_settingsWnd) { TmUI::Destroy(m_settingsWnd); m_settingsWnd = nullptr; }
    m_media.Shutdown();
    m_renderer.Shutdown();
    ClearOverlayImageCache();
    if (m_overlayLockInit) { DeleteCriticalSection(&m_overlayLock); m_overlayLockInit = false; }
    if (m_audioLockInit) { DeleteCriticalSection(&m_audioLock); m_audioLockInit = false; }
    GdiplusRelease();
}

// ─── IVdjPlugin8 ─────────────────────────────────────────────────────────────
HRESULT VDJ_API TmPlugin::OnLoad()
{
    TM_SEP();
    TM_INFO("TmPlugin::OnLoad");

    DeclareParameterButton(&m_nOpenFile, PARAM_OPEN_FILE, "Open File", "Open");
    DeclareParameterButton(&m_nPlay,     PARAM_PLAY,      "Play",      "Play");
    DeclareParameterButton(&m_nStop,     PARAM_STOP,      "Stop",      "Stop");
    DeclareParameterButton(&m_nPause,    PARAM_PAUSE,     "Pause",     "Pause");
    DeclareParameterSlider(&m_fVolume,   PARAM_VOLUME,    "Volume",    "Vol",  1.0f);
    DeclareParameterSlider(&m_fAlpha,    PARAM_ALPHA,     "Alpha",     "Alpha",1.0f);
    DeclareParameterSwitch(&m_bLooping,  PARAM_LOOP,      "Loop",      "Loop", true);

    TmLicense::LoadFromRegistry(&m_license);
    TmLicense::LoadSecureCredentials(&m_license);
    if (m_license.licenseKey[0] && m_license.authToken[0]) {
        TmLicense::Validate(&m_license);
    }

    TM_INFO("License startup state: status=%d licensed=%d key=%s email=%S",
            m_license.status, m_license.licensed, m_license.licenseKey, m_license.userEmail);
    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnGetPluginInfo(TVdjPluginInfo8* info)
{
    info->PluginName  = "TellyMedia v4";
    info->Author      = "DJ Micky K";
    info->Description  = "TellyMedia v4 - video, image & media playback for VirtualDJ";
    info->Version     = TELLYMEDIA_VERSION;
    info->Bitmap      = nullptr;
    info->Flags       = VDJFLAG_VIDEO_MASTERONLY;
    return S_OK;
}

ULONG VDJ_API TmPlugin::Release() { delete this; return S_OK; }

HRESULT VDJ_API TmPlugin::OnParameter(int id)
{
    switch (id) {
    case PARAM_PLAY:  if (m_nPlay)  m_playing = true;  break;
    case PARAM_STOP:  if (m_nStop)  m_playing = false; break;
    case PARAM_PAUSE: if (m_nPause) m_playing = false; break;
    case PARAM_LOOP:  break;
    default: break;
    }
    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnGetUserInterface(TVdjPluginInterface8* pi)
{
    if (!m_settingsWnd)
        m_settingsWnd = TmUI::CreateEmbedded(hInstance, this);

    pi->Type = VDJINTERFACE_DIALOG;
    pi->hWnd = m_settingsWnd;
    return S_OK;
}

// ─── IVdjPluginVideoFx8 ──────────────────────────────────────────────────────
HRESULT VDJ_API TmPlugin::OnStart() { TM_INFO("OnStart"); return S_OK; }
HRESULT VDJ_API TmPlugin::OnStop()  { TM_INFO("OnStop");  return S_OK; }

HRESULT VDJ_API TmPlugin::OnDeviceInit()
{
    TM_SEP();
    TM_INFO("OnDeviceInit - output %dx%d", width, height);

    ID3D11Device* dev = nullptr;
    HRESULT hr = GetDevice(VdjVideoEngineDirectX11, (void**)&dev);
    TM_HR("GetDevice(DX11)", hr);
    if (FAILED(hr) || !dev) {
        TM_ERR("No D3D11 device - OnDraw will pass deck through");
        return S_OK;
    }
    m_d3dDevice = dev;
    m_d3dDevice->GetImmediateContext(&m_d3dContext);
    TM_INFO("D3D11 device=%p context=%p", m_d3dDevice, m_d3dContext);

    m_d3dReady = m_renderer.Init(m_d3dDevice, m_d3dContext);
    TM_INFO("OnDeviceInit complete - rendererReady=%s", m_d3dReady ? "YES" : "NO");
    BuildOverlayPanelTextures();

    // Load custom shaders from shaders folder next to DLL
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW((HINSTANCE)hInstance, dllPath, MAX_PATH);

    // Shaders are loaded exclusively from the VirtualDJ shaders folder.
    // No built-in/hardcoded shaders are generated by the plugin.
    m_renderer.LoadShaders(dllPath);

    return S_OK;
}

HRESULT VDJ_API TmPlugin::OnDeviceClose()
{
    TM_INFO("OnDeviceClose");
    ReleaseOverlayPanelTextures();
    m_renderer.Shutdown();
    if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = nullptr; }
    m_d3dDevice = nullptr;   // owned by VirtualDJ
    m_d3dReady = false;
    return S_OK;
}

// Simple 2nd-order IIR bandpass filter (biquad)
static float ProcessBand(float sample, float* state, float freq, float q, float sampleRate)
{
    // Coefficients for bandpass filter
    float w0 = 2.0f * 3.14159265f * freq / sampleRate;
    float alpha = sinf(w0) / (2.0f * q);
    float b0 = alpha;
    float b1 = 0.0f;
    float b2 = -alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosf(w0);
    float a2 = 1.0f - alpha;

    // Normalize coefficients
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;

    // Process sample (direct form II)
    float out = b0 * sample + b1 * state[0] + b2 * state[1] - a1 * state[2] - a2 * state[3];
    state[3] = state[2];
    state[2] = out;
    state[1] = state[0];
    state[0] = sample;
    return out;
}

HRESULT VDJ_API TmPlugin::OnAudioSamples(float* buf, int nb)
{
    static int audioSampleCount = 0;
    if (++audioSampleCount % 60 == 0) {
        TM_INFO("OnAudioSamples called: buf=%p nb=%d", buf, nb);
    }
    if (!buf || nb <= 0) return S_OK;

    EnterCriticalSection(&m_audioLock);

    // Update sample rate from VDJ
    m_audio.sampleRate = SampleRate > 0 ? SampleRate : 44100;

    // Compute RMS level
    float sumSq = 0.0f;
    for (int i = 0; i < nb; ++i) {
        sumSq += buf[i] * buf[i];
    }
    float rms = sqrtf(sumSq / nb);
    // Smooth level with attack/decay
    const float attack = 0.1f;
    const float decay = 0.001f;
    if (rms > m_audio.level) {
        m_audio.level = m_audio.level + (rms - m_audio.level) * attack;
    } else {
        m_audio.level = m_audio.level + (rms - m_audio.level) * decay;
    }

    // Simplified: use RMS level for all bands (bandpass filter not working)
    // TODO: Fix bandpass filter for proper frequency separation
    m_audio.bass   = rms;
    m_audio.mid    = rms;
    m_audio.treble = rms;

    // Beat pulse from SongPosBeats fractional phase
    float bpm = SongBpm > 0 ? SongBpm : 120.0f;
    float beatPhase = SongPosBeats - floorf(SongPosBeats);
    // Create a short pulse at each beat (phase near 0)
    float pulse = 1.0f - beatPhase;
    if (pulse > 0.9f) pulse = (1.0f - pulse) * 10.0f; // sharpen
    else pulse = 0.0f;
    // Smooth beat
    const float beatAttack = 0.3f;
    const float beatDecay = 0.05f;
    if (pulse > m_audio.beat) {
        m_audio.beat = m_audio.beat + (pulse - m_audio.beat) * beatAttack;
    } else {
        m_audio.beat = m_audio.beat + (pulse - m_audio.beat) * beatDecay;
    }

    // Bass-based beat detection - pulse when bass crosses threshold
    static float bassEnvelope = 0.0f;
    static float lastBass = 0.0f;
    const float bassAttack = 0.5f;
    const float bassDecay = 0.02f;
    if (m_audio.bass > bassEnvelope) {
        bassEnvelope = bassEnvelope + (m_audio.bass - bassEnvelope) * bassAttack;
    } else {
        bassEnvelope = bassEnvelope + (m_audio.bass - bassEnvelope) * bassDecay;
    }
    // Detect bass peaks (when bass rises quickly)
    float bassDelta = m_audio.bass - lastBass;
    if (bassDelta > 0.05f && m_audio.bass > 0.1f) {
        // Bass peak detected - boost beat
        m_audio.beat = max(m_audio.beat, m_audio.bass * 2.0f);
    }
    lastBass = m_audio.bass;

    LeaveCriticalSection(&m_audioLock);
    return S_OK;
}

// Render panels to a BGRA buffer (1920x1080). Cached images (one per panel
// index, may be null) are passed in so we never touch the disk per frame.
static void RenderPanelsToBufferStatic(TmPlugin* plugin, const OverlayPanel* panels, Gdiplus::Image* const* images,
                                       int numPanels, BYTE* buffer, int w, int h)
{
    // Clear buffer to transparent
    memset(buffer, 0, w * h * 4);

    using namespace Gdiplus;
    Bitmap bmp(w, h, w * 4, PixelFormat32bppARGB, buffer);
    Graphics graphics(&bmp);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Sort panels by z-order
    int sorted[MAX_OVERLAY_PANELS];
    for (int i = 0; i < numPanels; ++i) sorted[i] = i;
    for (int i = 0; i < numPanels - 1; ++i)
        for (int j = i + 1; j < numPanels; ++j)
            if (panels[sorted[i]].zOrder > panels[sorted[j]].zOrder)
                { int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }

    for (int i = 0; i < numPanels; ++i) {
        const OverlayPanel& p = panels[sorted[i]];
        if (!p.visible) continue;
        if (p.panelType == 0) continue; // Skip Main Panel - it's for deck video, not overlay

        int px = (int)(p.x * w);
        int py = (int)(p.y * h);
        int pw = (int)(p.w * w);
        int ph = (int)(p.h * h);

        // Draw background
        if (p.hasBgColor) {
            Color bg(p.bgOpacity, GetRValue(p.bgColor), GetGValue(p.bgColor), GetBValue(p.bgColor));
            SolidBrush brush(bg);
            graphics.FillRectangle(&brush, px, py, pw, ph);
        }

        // Draw image if present (use the pre-loaded cached image, never load from disk here)
        if (p.panelType == 1 && p.hasImage) {
            Image* img = images ? images[sorted[i]] : nullptr;
            if (img && img->GetLastStatus() == Ok) {
                graphics.DrawImage(img, px, py, pw, ph);
            }
        }

        // Draw text if present
        if (p.panelType == 2 && p.textContent[0]) {
            Gdiplus::Color textColor = ColorFromRgbAndAlpha(p.textColor, p.textAlpha ? p.textAlpha : 255);
            Gdiplus::Color textColor2 = ColorFromRgbAndAlpha(p.textColor2, p.textAlpha2 ? p.textAlpha2 : (p.textAlpha ? p.textAlpha : 255));
            Gdiplus::RectF rect((Gdiplus::REAL)px, (Gdiplus::REAL)py, (Gdiplus::REAL)pw, (Gdiplus::REAL)ph);
            DrawAnimatedTextPanel(graphics, p, rect, textColor, textColor2);
        }
        // Song / Now Playing panel
        else if (p.panelType == 4) {
            COLORREF panelBg = p.bgColor;
            if (p.hasBgColor) {
                Gdiplus::Color bgColor((BYTE)p.bgOpacity,
                    GetRValue(panelBg), GetGValue(panelBg), GetBValue(panelBg));
                Gdiplus::SolidBrush bg(bgColor);
                graphics.FillRectangle(&bg, px, py, pw, ph);
            }

            wchar_t songText[512] = {};
            if (plugin) plugin->HostGetNowPlayingText(songText, _countof(songText));
            if (!songText[0]) wcscpy_s(songText, L"No Track Playing");

            // Rainbow color cycling
            static DWORD rainbowTick = 0;
            rainbowTick++;
            float hue = (float)(rainbowTick % 360) / 360.0f;

            COLORREF headingColorRef = p.headingTextColor;
            Gdiplus::Color headingColor(255,
                GetRValue(headingColorRef), GetGValue(headingColorRef), GetBValue(headingColorRef));
            if (p.rainbowColor) {
                headingColor = HSVtoRGB(hue, 1.0f, 1.0f);
            }
            Gdiplus::SolidBrush titleBrush(headingColor);
            COLORREF songColorRef = p.songTextColor;
            Gdiplus::Color songColor(255,
                GetRValue(songColorRef), GetGValue(songColorRef), GetBValue(songColorRef));
            Gdiplus::SolidBrush bodyBrush(songColor);

            Gdiplus::FontFamily headingFamily(p.headingFontName[0] ? p.headingFontName : (p.fontName[0] ? p.fontName : L"Segoe UI"));
            Gdiplus::FontFamily songFamily(p.songFontName[0] ? p.songFontName : (p.fontName[0] ? p.fontName : L"Segoe UI"));
            Gdiplus::Font titleFont(&headingFamily, (Gdiplus::REAL)p.fontSize,
                Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font bodyFont(&songFamily, (Gdiplus::REAL)max(18, p.fontSize - 8),
                Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

            // Calculate base animated dimensions first
            float animScale = 1.0f;
            if (p.songAnimMode == 1) { // Pulse
                animScale = (float)sin(rainbowTick * 0.1f) * 0.1f + 1.0f;
            }
            int animPw = (int)(pw * animScale);
            int animPh = (int)(ph * animScale);

            // Animation offsets
            float animOffsetY = 0.0f;
            float animOffsetX = 0.0f;
            if (p.songAnimMode == 2) { // Wave
                animOffsetY = (float)sin(rainbowTick * 0.15f) * 5.0f;
            } else if (p.songAnimMode == 5) { // Bounce left to right
                float bounce = (float)sin(rainbowTick * 0.08f);
                animOffsetX = bounce * (animPw * 0.15f);
            } else if (p.songAnimMode == 6) { // Scroll left to right (fast)
                animOffsetX = (float)(rainbowTick % max(1, animPw * 2)) * 1.5f - (animPw * 0.2f);
            } else if (p.songAnimMode == 7) { // Scroll 1-line mode
                animOffsetX = (float)(rainbowTick % max(1, animPw * 3)) * 2.0f - (animPw * 0.3f);
            }

            int animPx = px + (int)((pw * (1.0f - animScale)) / 2) + (int)animOffsetX;
            int animPy = py + (int)((ph * (1.0f - animScale)) / 2) + (int)animOffsetY;

            // Set up text alignment
            Gdiplus::StringFormat textFmt;
            if (p.textAlign == 1) { // Center
                textFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                textFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            } else if (p.textAlign == 2) { // Right
                textFmt.SetAlignment(Gdiplus::StringAlignmentFar);
                textFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
            } else { // Left (default)
                textFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                textFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
            }
            textFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

            // Scroll 1-line mode: combine heading + song on one line
            if (p.songAnimMode == 7) {
                wchar_t combinedText[640];
                swprintf_s(combinedText, L"%s - %s", 
                    p.textContent[0] ? p.textContent : L"Now Playing", songText);
                
                Gdiplus::RectF combinedRect((Gdiplus::REAL)(animPx + 12), (Gdiplus::REAL)(animPy + 10),
                                          (Gdiplus::REAL)(animPw - 24), (Gdiplus::REAL)(animPh - 20));
                graphics.DrawString(combinedText, -1, &titleFont, combinedRect, &textFmt, &titleBrush);
            } else {
                // Normal 2-line mode
                wchar_t heading[128];
                swprintf_s(heading, L"%s", p.textContent[0] ? p.textContent : L"Now Playing");

                // Calculate heading height based on font size with minimum gap
                int headingHeight = max(30, p.fontSize + 8);
                int headingBottom = animPy + 10 + headingHeight;
                int songTop = headingBottom + 8; // 8px gap between heading and song

                Gdiplus::RectF headingRect((Gdiplus::REAL)(animPx + 12), (Gdiplus::REAL)(animPy + 10),
                                           (Gdiplus::REAL)(animPw - 24), (Gdiplus::REAL)headingHeight);
                graphics.DrawString(heading, -1, &titleFont, headingRect, &textFmt, &titleBrush);

                // Use center alignment for song text in wave mode
                Gdiplus::StringFormat songFmt;
                if (p.songAnimMode == 2 && p.textAlign == 0) { // Wave + Left align = center song
                    songFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    songFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                } else if (p.textAlign == 1) { // Center
                    songFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    songFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                } else if (p.textAlign == 2) { // Right
                    songFmt.SetAlignment(Gdiplus::StringAlignmentFar);
                    songFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
                } else { // Left (default)
                    songFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                    songFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
                }
                songFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

                Gdiplus::RectF songRect((Gdiplus::REAL)(animPx + 12), (Gdiplus::REAL)songTop,
                                        (Gdiplus::REAL)(animPw - 24), (Gdiplus::REAL)(animPh - (songTop - animPy) - 10));
                graphics.DrawString(songText, -1, &bodyFont, songRect, &songFmt, &bodyBrush);
            }
        }
    }
}

HRESULT VDJ_API TmPlugin::OnDraw()
{
    static int drawCount = 0;
    static LARGE_INTEGER qpcFreq = {};
    static bool qpcInit = false;
    if (!qpcInit) { QueryPerformanceFrequency(&qpcFreq); qpcInit = true; }
    LARGE_INTEGER frameStart = {};
    QueryPerformanceCounter(&frameStart);
    drawCount++;
    if (drawCount % 60 == 0) {
        TM_INFO("OnDraw frame %d - d3dReady=%s playing=%s numPanels=%d",
                drawCount, m_d3dReady ? "YES" : "NO", m_playing ? "YES" : "NO", m_numOverlayPanels);
    }

    // Find the visible Main Panel (type=0) anywhere in the panel array
    bool hasMainPanel = false;
    float mainX = 0, mainY = 0, mainW = 1, mainH = 1;
    for (int i = 0; i < m_numOverlayPanels; ++i) {
        if (m_overlayPanels[i].panelType == 0 && m_overlayPanels[i].visible) {
            float x = m_overlayPanels[i].x, y = m_overlayPanels[i].y;
            float w = m_overlayPanels[i].w, h = m_overlayPanels[i].h;
            // Clamp to valid range so dragging/resizing never loses the deck
            if (w < 0.01f) w = 0.01f;
            if (h < 0.01f) h = 0.01f;
            if (x < 0.f) x = 0.f;
            if (y < 0.f) y = 0.f;
            if (x + w > 1.f) w = 1.f - x;
            if (y + h > 1.f) h = 1.f - y;
            hasMainPanel = true;
            mainX = x; mainY = y; mainW = w; mainH = h;
            if (drawCount % 60 == 0) {
                TM_INFO("Main Panel[%d]: x=%.3f y=%.3f w=%.3f h=%.3f", i, mainX, mainY, mainW, mainH);
            }
            break;
        }
    }

    if (!m_d3dReady) {
        // If D3D not ready, just draw deck fullscreen
        DrawDeck();
        return S_OK;
    }

    EnterCriticalSection(&m_overlayLock);

    // Get VDJ's deck texture (vertices carry the correct UVs for the video region)
    TVertex* deckVtx = nullptr;
    ID3D11ShaderResourceView* deckSRV = nullptr;
    GetTexture(VdjVideoEngineDirectX11, (void**)&deckSRV, &deckVtx);

    if (drawCount % 60 == 0) {
        TM_INFO("deckSRV=%p deckVtx=%p hasMainPanel=%s", deckSRV, deckVtx, hasMainPanel ? "YES" : "NO");
    }

    // ─── Detect karaoke and audio-only state (throttled) ───────────────────────
    // Check karaoke every 30 frames (~0.5s at 60fps)
    if (drawCount - m_karaokeCheckFrame > 30) {
        m_karaokeCheckFrame = drawCount;
        RefreshNowPlayingCache();

        bool isKaraoke = false;

        char karaokeA[32] = {};
        if (SUCCEEDED(GetStringInfo("get_karaoke ? on : off", karaokeA, sizeof(karaokeA)))) {
            _strlwr_s(karaokeA);
            isKaraoke = (strcmp(karaokeA, "on") == 0);
        }

        if (!isKaraoke) {
            char typeA[64] = {};
            if (SUCCEEDED(GetStringInfo("get_loaded_song 'type'", typeA, sizeof(typeA)))) {
                _strlwr_s(typeA);
                if (strcmp(typeA, "karaoke") == 0) {
                    isKaraoke = true;
                }
            }
        }

        if (!isKaraoke && m_currentPath[0]) {
            isKaraoke = DetectKaraokeTrack(m_currentPath);
        }

        m_isKaraoke = isKaraoke;
        if (drawCount % 60 == 0) {
            TM_INFO("Karaoke detection: %s", m_isKaraoke ? "YES" : "NO");
        }
    }

    // Check audio-only (no video) every 30 frames
    if (drawCount - m_audioOnlyCheckFrame > 30) {
        m_audioOnlyCheckFrame = drawCount;
        // Check if current track is a video file
        m_isAudioOnly = !TmMedia::IsVideoPath(m_currentPath);
        // Also check if deckSRV is null (no video content from VDJ)
        if (!deckSRV) m_isAudioOnly = true;
        if (drawCount % 60 == 0) {
            TM_INFO("Audio-only detection: %s (path=%S)", m_isAudioOnly ? "YES" : "NO", m_currentPath);
        }
    }

    const bool karaokeBlocking = m_karaokeAutoHide && m_isKaraoke;

    // ─── Detect visible Shader panels (type 3) ─────────────────────────────────
    int shaderCount = m_renderer.GetShaderCount();
    int shaderPanelCount = 0;
    for (int i = 0; i < m_numOverlayPanels; ++i) {
        if (m_overlayPanels[i].panelType == 3 && m_overlayPanels[i].visible) shaderPanelCount++;
    }

    if (drawCount % 60 == 0) {
        TM_INFO("Shader decision: playing=%s shaderCount=%d shaderPanels=%d mainShaderIdx=%d",
                m_playing ? "YES" : "NO", shaderCount, shaderPanelCount, m_mainShaderIndex);
    }

    // ─── Save VDJ's D3D11 pipeline state before we touch anything ─────────────
    // Our renderer draws bind their own shaders/buffers/blend/layout on VDJ's
    // immediate context. If we don't restore them, VDJ's subsequent rendering
    // breaks and the output goes black. Mirror the deck's full state here.
    ID3D11DeviceContext* ctx = m_d3dContext;
    ID3D11VertexShader*       prevVS    = nullptr; UINT prevVSInst = 0;
    ID3D11PixelShader*        prevPS    = nullptr; UINT prevPSInst = 0;
    ID3D11ShaderResourceView* prevSRV   = nullptr;
    ID3D11SamplerState*       prevSamp  = nullptr;
    ID3D11Buffer*             prevCB    = nullptr;
    ID3D11BlendState*         prevBlend = nullptr;
    float                     prevBlendFactor[4] = {};
    UINT                      prevBlendMask = 0;
    ID3D11InputLayout*        prevIL    = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY  prevTopo  = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer*             prevVB    = nullptr; UINT prevVBStride = 0, prevVBOffset = 0;
    if (ctx) {
        ctx->VSGetShader(&prevVS, nullptr, &prevVSInst);
        ctx->PSGetShader(&prevPS, nullptr, &prevPSInst);
        ctx->PSGetShaderResources(0, 1, &prevSRV);
        ctx->PSGetSamplers(0, 1, &prevSamp);
        ctx->PSGetConstantBuffers(0, 1, &prevCB);
        ctx->OMGetBlendState(&prevBlend, prevBlendFactor, &prevBlendMask);
        ctx->IAGetInputLayout(&prevIL);
        ctx->IAGetPrimitiveTopology(&prevTopo);
        ctx->IAGetVertexBuffers(0, 1, &prevVB, &prevVBStride, &prevVBOffset);
    }

    // ─── Render deck video into the main panel (or fullscreen) ──────────────────
    {
        // VDJ vertex positions are in pixel space (0,0)=top-left, (width,height)=bottom-right.
        // Convert normalised panel coords to pixel coords.
        float outW = (float)width;
        float outH = (float)height;
        float pxX1 = mainX * outW;
        float pxY1 = mainY * outH;
        float pxX2 = (mainX + mainW) * outW;
        float pxY2 = (mainY + mainH) * outH;

        // Render VDJ deck video.
        // Strategy: mutate VDJ's own TVertex positions to the panel rect NDC bounds,
        // then call DrawDeck() so VDJ composites via its own pipeline into the panel
        // rectangle. This guarantees the output lands on the correct render target.
        // Restore the original vertices after so VDJ's bookkeeping stays intact.
        if (!karaokeBlocking && hasMainPanel && deckVtx) {
            // Save original vertex positions (4 verts)
            float origX[4], origY[4];
            for (int i = 0; i < 4; ++i) {
                origX[i] = deckVtx[i].position.x;
                origY[i] = deckVtx[i].position.y;
            }

            // Overwrite positions with Main Panel pixel-space bounds.
            // VDJ vertex order: [0]=TL [1]=TR [2]=BL [3]=BR  (pixel coords)
            deckVtx[0].position.x = pxX1; deckVtx[0].position.y = pxY1;
            deckVtx[1].position.x = pxX2; deckVtx[1].position.y = pxY1;
            deckVtx[2].position.x = pxX2; deckVtx[2].position.y = pxY2;
            deckVtx[3].position.x = pxX1; deckVtx[3].position.y = pxY2;
            DrawDeck();
            // Restore original positions.
            for (int i = 0; i < 4; ++i) {
                deckVtx[i].position.x = origX[i];
                deckVtx[i].position.y = origY[i];
            }
        } else {
            // No Main Panel - pass through VDJ's native fullscreen deck render.
            DrawDeck();
            if (drawCount % 60 == 0) {
                TM_INFO("No Main Panel - DrawDeck() fullscreen pass-through");
            }
        }
    }

    // Capture the current media frame into m_slideshowFrameCache scaled to panel size.
    // The actual draw happens below via the CPU overlay buffer (same path as image panels).
    // Keep the previous valid frame alive until a new one is successfully captured so
    // transitions never fall back to a cut just because the next frame is still decoding.
    if (!karaokeBlocking && m_playing) {
        const void* pixels = nullptr;
        int fw = 0, fh = 0, fstride = 0;
        bool gotFrame = m_media.BeginFrame(&pixels, &fw, &fh, &fstride);

        if (gotFrame && fw > 0 && fh > 0) {
            // Use panel size if Main Panel exists, otherwise use full 1920x1080
            int panW, panH;
            if (hasMainPanel) {
                panW = max(1, (int)(mainW * 1920.f));
                panH = max(1, (int)(mainH * 1080.f));
                if (panW > 1920) panW = 1920;
                if (panH > 1080) panH = 1080;
            } else {
                panW = 1920;
                panH = 1080;
            }
            m_slideshowFrameCache.resize(panW * panH * 4);
            using namespace Gdiplus;
            Bitmap srcBmp(fw, fh, fstride, PixelFormat32bppPARGB, (BYTE*)pixels);
            Bitmap dstBmp(panW, panH, PixelFormat32bppARGB);
            Graphics g(&dstBmp);
            g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
            g.DrawImage(&srcBmp, 0, 0, panW, panH);
            BitmapData bd = {};
            Rect rc(0, 0, panW, panH);
            if (dstBmp.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) == Ok) {
                for (int row = 0; row < panH; ++row)
                    memcpy(m_slideshowFrameCache.data() + row * panW * 4,
                           (BYTE*)bd.Scan0 + row * bd.Stride, panW * 4);
                dstBmp.UnlockBits(&bd);
                m_slideshowFrameW = panW;
                m_slideshowFrameH = panH;
                m_slideshowFrameValid = true;

                // Blend prev frame over current for fade/crossfade.
                // Progress only advances once we have the new frame - this ensures the
                // blend starts as soon as the first frame of the new image is ready.
                if ((m_transitionMode == 1 || m_transitionMode == 2)
                    && m_slideshowPrevValid && m_transitionProgress < 1.0f) {
                    // ~1.5s at 60 fps
                    m_transitionProgress += 1.0f / 90.0f;
                    if (m_transitionProgress > 1.0f) m_transitionProgress = 1.0f;

                    // Ensure prev cache is at the same dimensions (rescale if needed)
                    if (m_slideshowPrevW != panW || m_slideshowPrevH != panH
                        || (int)m_slideshowPrevCache.size() != panW * panH * 4) {
                        // Rescale prev snapshot to current panel size
                        std::vector<BYTE> rescaled(panW * panH * 4);
                        Bitmap prevBmp(m_slideshowPrevW, m_slideshowPrevH,
                                       m_slideshowPrevW * 4, PixelFormat32bppARGB,
                                       m_slideshowPrevCache.data());
                        Bitmap scaledPrev(panW, panH, PixelFormat32bppARGB);
                        Graphics gs(&scaledPrev);
                        gs.SetInterpolationMode(InterpolationModeHighQualityBilinear);
                        gs.DrawImage(&prevBmp, 0, 0, panW, panH);
                        BitmapData bds = {};
                        Rect rcs(0, 0, panW, panH);
                        if (scaledPrev.LockBits(&rcs, ImageLockModeRead, PixelFormat32bppARGB, &bds) == Ok) {
                            for (int row = 0; row < panH; ++row)
                                memcpy(rescaled.data() + row * panW * 4,
                                       (BYTE*)bds.Scan0 + row * bds.Stride, panW * 4);
                            scaledPrev.UnlockBits(&bds);
                        }
                        m_slideshowPrevCache = std::move(rescaled);
                        m_slideshowPrevW = panW;
                        m_slideshowPrevH = panH;
                    }

                    // CPU lerp: blended = lerp(prev, current, progress)
                    const int numBytes = panW * panH * 4;
                    const BYTE* prev = m_slideshowPrevCache.data();
                    BYTE* cur = m_slideshowFrameCache.data();
                    const int t  = (int)(m_transitionProgress * 255.0f + 0.5f);
                    const int it = 255 - t;
                    for (int p = 0; p < numBytes; ++p)
                        cur[p] = (BYTE)((cur[p] * t + prev[p] * it + 127) / 255);

                    if (m_transitionProgress >= 1.0f)
                        m_slideshowPrevValid = false;
                }
            }
            m_media.EndFrame();

        } else if (gotFrame && fw > 0 && fh > 0 && !hasMainPanel) {
            // No panel: full-frame D3D path with crossfade
            m_renderer.UploadFrame(pixels, fw, fh, fstride);
            m_media.EndFrame();
            if ((m_transitionMode == 1 || m_transitionMode == 2) && m_transitionProgress < 1.0f) {
                m_transitionProgress += 1.0f / 90.0f;
                if (m_transitionProgress > 1.0f) m_transitionProgress = 1.0f;
                m_renderer.DrawCrossfade(m_fAlpha, m_transitionProgress);
            } else {
                m_renderer.DrawFullscreen(m_fAlpha);
            }

        } else if (!gotFrame && m_slideshowPrevValid
                   && m_slideshowPrevW > 0 && m_slideshowPrevH > 0) {
            // New media not decoded yet - hold the prev frame so screen doesn't blank
            m_slideshowFrameCache = m_slideshowPrevCache;
            m_slideshowFrameW     = m_slideshowPrevW;
            m_slideshowFrameH     = m_slideshowPrevH;
            m_slideshowFrameValid = true;

        } else if (gotFrame) {
            m_media.EndFrame();
        }
    }

    // Render overlay panels to video output (skip Main Panel since it's for video content)
    m_renderModeEffective = TM_RENDER_CPU;
    if (!karaokeBlocking && m_numOverlayPanels > 0) {
        static int frameCount = 0;
        static bool loggedContent = false;
        static BYTE overlayBuffer[1920 * 1080 * 4] = {};

        bool useGpuImages = false;
        m_renderModeEffective = TM_RENDER_CPU;
        bool needCpuOverlay = m_slideshowFrameValid;
        if (!needCpuOverlay) {
            for (int i = 0; i < m_numOverlayPanels; ++i) {
                if (!m_overlayPanels[i].visible) continue;
                if (m_overlayPanels[i].panelType == 0) continue;
                needCpuOverlay = true;
                break;
            }
        }
        
        // Log panel content for debugging (once)
        if (!loggedContent) {
            for (int i = 0; i < m_numOverlayPanels; ++i) {
                TM_INFO("Panel %d: type=%d visible=%d hasImage=%d hasBgColor=%d text='%S' imagePath='%S'",
                        i, m_overlayPanels[i].panelType, m_overlayPanels[i].visible,
                        m_overlayPanels[i].hasImage, m_overlayPanels[i].hasBgColor,
                        m_overlayPanels[i].textContent, m_overlayPanels[i].imagePath);
            }
            loggedContent = true;
        }

        // Render overlay buffer (text, images, shader fallback, etc.) in one CPU pass
        static int overlayLogCount = 0;
        if (++overlayLogCount % 60 == 0) {
            TM_INFO("Overlay buffer: needCpuOverlay=%d useGpuImages=%d", needCpuOverlay, useGpuImages);
        }
        if (needCpuOverlay) {
            TM_INFO("Rendering overlay buffer fullscreen");
            // Pass slideshow frame so it blits at the main panel's z-order position.
            const BYTE* ssFrame = (m_slideshowFrameValid && m_slideshowFrameW > 0 && m_slideshowFrameH > 0)
                                   ? m_slideshowFrameCache.data() : nullptr;
            RenderPanelsToBuffer(m_overlayPanels, m_overlayImages, m_overlayImageSRVs, m_numOverlayPanels,
                                  overlayBuffer, 1920, 1080, useGpuImages,
                                  ssFrame, m_slideshowFrameW, m_slideshowFrameH);

            m_renderer.UploadOverlay(overlayBuffer, 1920, 1080, 1920 * 4);
            m_renderer.DrawOverlay();
        }

        if (frameCount++ % 60 == 0) {
            TM_INFO("OnDraw: rendered %d overlay panels to video output", m_numOverlayPanels);
        }
    }

    // ─── Draw watermark if not licensed (on top of all panels and slideshow) ─────
    if (m_license.status != LIC_VALID) {
        static BYTE watermarkBuffer[1920 * 1080 * 4] = {};
        
        // Watermark 1
        static float watermark1X = 960.0f;
        static float watermark1Y = 540.0f;
        static float velocity1X = 3.0f;
        static float velocity1Y = 2.0f;
        
        // Watermark 2
        static float watermark2X = 480.0f;
        static float watermark2Y = 270.0f;
        static float velocity2X = -2.5f;
        static float velocity2Y = 3.5f;
        
        // Watermark 3
        static float watermark3X = 1440.0f;
        static float watermark3Y = 810.0f;
        static float velocity3X = -3.5f;
        static float velocity3Y = -2.5f;
        
        // Update positions every frame (bounce off screen edges)
        watermark1X += velocity1X;
        watermark1Y += velocity1Y;
        if (watermark1X < 300 || watermark1X > 1620) velocity1X = -velocity1X;
        if (watermark1Y < 50 || watermark1Y > 1030) velocity1Y = -velocity1Y;
        
        watermark2X += velocity2X;
        watermark2Y += velocity2Y;
        if (watermark2X < 300 || watermark2X > 1620) velocity2X = -velocity2X;
        if (watermark2Y < 50 || watermark2Y > 1030) velocity2Y = -velocity2Y;
        
        watermark3X += velocity3X;
        watermark3Y += velocity3Y;
        if (watermark3X < 300 || watermark3X > 1620) velocity3X = -velocity3X;
        if (watermark3Y < 50 || watermark3Y > 1030) velocity3Y = -velocity3Y;
        
        // Clear buffer to transparent
        memset(watermarkBuffer, 0, sizeof(watermarkBuffer));
        
        using namespace Gdiplus;
        Bitmap bmp(1920, 1080, 1920 * 4, PixelFormat32bppARGB, watermarkBuffer);
        Graphics g(&bmp);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetCompositingMode(CompositingModeSourceOver);
        
        // Draw watermark text with black outline
        const wchar_t* watermarkText = L"UNLICENSED - Please activate TellyMedia";
        FontFamily fontFamily(L"Arial");
        Font font(&fontFamily, 48, FontStyleBold, UnitPixel);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        
        SolidBrush blackBrush(Color(255, 0, 0, 0));
        SolidBrush whiteBrush(Color(255, 255, 255, 255));
        
        // Draw all 3 watermarks
        float positions[3][2] = {{watermark1X, watermark1Y}, {watermark2X, watermark2Y}, {watermark3X, watermark3Y}};
        
        for (int i = 0; i < 3; i++) {
            float wx = positions[i][0];
            float wy = positions[i][1];
            RectF textBounds(wx - 300, wy - 50, 600, 100);
            
            // Draw black outline
            for (int dx = -2; dx <= 2; dx++) {
                for (int dy = -2; dy <= 2; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    RectF outlineBounds(wx - 300 + dx, wy - 50 + dy, 600, 100);
                    g.DrawString(watermarkText, -1, &font, outlineBounds, &format, &blackBrush);
                }
            }
            
            // Draw white text on top
            g.DrawString(watermarkText, -1, &font, textBounds, &format, &whiteBrush);
        }
        
        // Draw static "Buy License" watermark at top right corner
        const wchar_t* buyText = L"Buy License at https://djeventsuite.cloud/";
        Font buyFont(&fontFamily, 36, FontStyleBold, UnitPixel);
        RectF buyBounds(1400, 50, 500, 60);
        
        // Draw black outline for buy text
        for (int dx = -2; dx <= 2; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                if (dx == 0 && dy == 0) continue;
                RectF outlineBounds(1400 + dx, 50 + dy, 500, 60);
                g.DrawString(buyText, -1, &buyFont, outlineBounds, &format, &blackBrush);
            }
        }
        
        // Draw white text on top
        g.DrawString(buyText, -1, &buyFont, buyBounds, &format, &whiteBrush);
        
        // Upload and draw watermark overlay on top of everything
        m_renderer.UploadOverlay(watermarkBuffer, 1920, 1080, 1920 * 4);
        m_renderer.DrawOverlay();
    }

    LeaveCriticalSection(&m_overlayLock);

    LARGE_INTEGER frameEnd = {};
    QueryPerformanceCounter(&frameEnd);
    double frameMs = (double)(frameEnd.QuadPart - frameStart.QuadPart) * 1000.0 / (double)qpcFreq.QuadPart;
    if (m_renderAvgMs <= 0.0f) m_renderAvgMs = (float)frameMs;
    else m_renderAvgMs = (m_renderAvgMs * 0.9f) + ((float)frameMs * 0.1f);

    // ─── Restore VDJ's pipeline state exactly as we found it ──────────────────
    if (ctx) {
        ctx->VSSetShader(prevVS, nullptr, 0);
        ctx->PSSetShader(prevPS, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &prevSRV);
        ctx->PSSetSamplers(0, 1, &prevSamp);
        ctx->PSSetConstantBuffers(0, 1, &prevCB);
        ctx->OMSetBlendState(prevBlend, prevBlendFactor, prevBlendMask);
        ctx->IASetInputLayout(prevIL);
        ctx->IASetPrimitiveTopology(prevTopo);
        ctx->IASetVertexBuffers(0, 1, &prevVB, &prevVBStride, &prevVBOffset);

        // Release the refs returned by the *Get* calls (one ref each, may be null).
        if (prevVS)    prevVS->Release();
        if (prevPS)    prevPS->Release();
        if (prevSRV)   prevSRV->Release();
        if (prevSamp)  prevSamp->Release();
        if (prevCB)    prevCB->Release();
        if (prevBlend) prevBlend->Release();
        if (prevIL)    prevIL->Release();
        if (prevVB)    prevVB->Release();
    }

    return S_OK;
}

// ─── ITmUIHost (UI -> plugin) ────────────────────────────────────────────────
void TmPlugin::HostPlayMedia(const wchar_t* path, int scaleMode)
{
    if (!path || !path[0]) return;
    TM_INFO("HostPlayMedia: %S (scale=%d)", path, scaleMode);
    m_scaleMode = scaleMode;
    wcscpy_s(m_currentPath, path);

    // For crossfade/fade: snapshot current CPU frame cache into prev cache before loading new media
    EnterCriticalSection(&m_overlayLock);
    const bool transitionActive = (m_transitionMode == 1 || m_transitionMode == 2) && m_playing;
    if (transitionActive
        && m_slideshowFrameValid && m_slideshowFrameW > 0 && m_slideshowFrameH > 0) {
        // Main-panel CPU-blend path has a captured frame to blend from.
        m_slideshowPrevCache = m_slideshowFrameCache;
        m_slideshowPrevW     = m_slideshowFrameW;
        m_slideshowPrevH     = m_slideshowFrameH;
        m_slideshowPrevValid = true;
    } else {
        m_slideshowPrevValid = false;
    }
    // Reset progress whenever a transition is active so BOTH the CPU-blend path
    // and the no-panel fullscreen path can blend. The fullscreen path relies on
    // the renderer's saved previous frame (below) rather than the CPU cache.
    m_transitionProgress = transitionActive ? 0.0f : 1.0f;
    LeaveCriticalSection(&m_overlayLock);

    // Also save into renderer for the no-panel fullscreen path (both fade & crossfade).
    if (transitionActive) {
        m_renderer.SaveCurrentAsPrevious();
    }

    // Open new media (this will stop the current one internally)
    if (m_media.Open(path, scaleMode)) {
        m_media.SetLooping(m_bLooping != 0);
        m_media.Play();
        m_playing = true;
        // Reset transition alpha for new media
        if (m_transitionMode == 1 || m_transitionMode == 2) m_transitionAlpha = 0.0f;
    } else {
        TM_WARN("HostPlayMedia: failed to open %S", path);
    }
}
void TmPlugin::HostStop()
{
    m_playing = false;
    m_media.Stop();
    // Clear the cached slideshow frame so the last image is not left frozen on
    // the output after the slideshow is disabled/stopped.
    EnterCriticalSection(&m_overlayLock);
    m_slideshowFrameValid = false;
    m_slideshowPrevValid  = false;
    m_transitionProgress  = 1.0f;
    LeaveCriticalSection(&m_overlayLock);
}
void TmPlugin::HostPause() { m_playing = false; m_media.Pause(); }
void TmPlugin::HostScaleMode(int mode) { m_scaleMode = mode; }
void TmPlugin::HostTransitionMode(int mode) { m_transitionMode = mode; TM_INFO("Transition mode: %d", mode); }
void TmPlugin::HostSlideshowStart() { TM_INFO("UI slideshow start"); }
void TmPlugin::HostSlideshowStop()
{
    TM_INFO("UI slideshow stop");
    HostStop();
}
void TmPlugin::HostRenderMode(int mode)
{
    if (mode < TM_RENDER_AUTO || mode > TM_RENDER_GPU) mode = TM_RENDER_AUTO;
    m_renderModeRequested = mode;
    TM_INFO("Render mode requested: %d", mode);
}
void TmPlugin::HostGetNowPlayingText(wchar_t* outText, int outTextMax)
{
    if (!outText || outTextMax <= 0) return;
    RefreshNowPlayingCache();
    wcscpy_s(outText, outTextMax, m_songDisplay[0] ? m_songDisplay : L"No Track Playing");
}
void TmPlugin::HostGetRenderModeText(wchar_t* outText, int outTextMax)
{
    if (!outText || outTextMax <= 0) return;
    const wchar_t* mode = L"Auto";
    if (m_renderModeRequested == TM_RENDER_CPU) mode = L"CPU";
    else if (m_renderModeRequested == TM_RENDER_GPU) mode = L"GPU";

    const wchar_t* effective = (m_renderModeEffective == TM_RENDER_GPU) ? L"GPU" : L"CPU";
    if (m_renderModeRequested == TM_RENDER_AUTO) {
        swprintf_s(outText, outTextMax, L"Render: Auto (%s)", effective);
    } else if (m_renderModeRequested == TM_RENDER_GPU && m_renderModeEffective != TM_RENDER_GPU) {
        swprintf_s(outText, outTextMax, L"Render: GPU (fallback CPU)");
    } else {
        swprintf_s(outText, outTextMax, L"Render: %s", mode);
    }
}

void TmPlugin::HostSetShadersEnabled(bool enabled)
{
    m_shadersEnabled = enabled;
    TM_INFO("Shaders enabled: %s", enabled ? "YES" : "NO");
}

void TmPlugin::HostSetShaderKaraokeDisable(bool disable)
{
    m_shaderDisableOnKaraoke = disable;
    TM_INFO("Shader disable on karaoke: %s", disable ? "YES" : "NO");
}

void TmPlugin::HostSetKaraokeAutoHide(bool enabled)
{
    m_karaokeAutoHide = enabled;
    TM_INFO("Karaoke auto-hide: %s", enabled ? "YES" : "NO");
}

void TmPlugin::HostSetMainShader(int index)
{
    m_mainShaderIndex = index;
    TM_INFO("Main shader index: %d", index);
}

void TmPlugin::HostReloadShaders()
{
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW((HINSTANCE)hInstance, dllPath, MAX_PATH);
    m_renderer.ReloadShaders(dllPath);
    TM_INFO("Shaders reloaded");
}

void TmPlugin::HostGetShaderNames(wchar_t* outNames, int outNamesMax)
{
    if (!outNames || outNamesMax <= 0) return;
    outNames[0] = L'\0';

    int count = m_renderer.GetShaderCount();
    int pos = 0;
    for (int i = 0; i < count && pos < outNamesMax - 2; ++i) {
        const wchar_t* name = m_renderer.GetShaderName(i);
        int len = (int)wcslen(name);
        if (pos + len + 2 < outNamesMax) {
            wcscpy_s(outNames + pos, outNamesMax - pos, name);
            pos += len;
            if (i < count - 1) {
                outNames[pos++] = L'|';
                outNames[pos] = L'\0';
            }
        }
    }
}

bool TmPlugin::HostGetLicenseState(LicenseState* outState)
{
    if (!outState) return false;
    *outState = m_license;
    return true;
}

bool TmPlugin::HostLicenseLogin(const wchar_t* email, const wchar_t* password, bool savePassword)
{
    m_license.savePassword = savePassword;
    const bool ok = TmLicense::Login(&m_license, email, password);
    if (!ok) {
        TM_WARN("License login failed: %s", m_license.message);
    }
    return ok;
}

bool TmPlugin::HostLicenseValidate()
{
    const bool ok = TmLicense::Validate(&m_license);
    TM_INFO("License validate result: %s", ok ? "OK" : "FAILED");
    return ok;
}

bool TmPlugin::HostLicenseActivate()
{
    const bool ok = TmLicense::ActivateDevice(&m_license);
    TM_INFO("License activate result: %s", ok ? "OK" : "FAILED");
    return ok;
}

void TmPlugin::HostLicenseLogout()
{
    TmLicense::Logout(&m_license);
}

// ─── Overlay panel management ─────────────────────────────────────────────
void TmPlugin::UISetOverlayPanels(const OverlayPanel* panels, int count)
{
    if (!panels || count < 0) return;
    if (count > MAX_OVERLAY_PANELS) count = MAX_OVERLAY_PANELS;

    EnterCriticalSection(&m_overlayLock);
    int oldCount = m_numOverlayPanels;

    // Release any slots that disappear in the new panel array.
    for (int i = count; i < oldCount; ++i) {
        if (m_overlayImageSRVs[i]) { m_overlayImageSRVs[i]->Release(); m_overlayImageSRVs[i] = nullptr; }
        if (m_overlayImages[i]) { delete m_overlayImages[i]; m_overlayImages[i] = nullptr; }
        ZeroMemory(&m_overlayPanels[i], sizeof(OverlayPanel));
    }

    m_numOverlayPanels = count;
    for (int i = 0; i < count; ++i) {
        const OverlayPanel& newPanel = panels[i];
        const bool sameImage = (i < oldCount &&
            m_overlayPanels[i].panelType == 1 &&
            newPanel.panelType == 1 &&
            m_overlayPanels[i].hasImage == newPanel.hasImage &&
            m_overlayPanels[i].imagePath[0] != L'\0' &&
            wcscmp(m_overlayPanels[i].imagePath, newPanel.imagePath) == 0);

        // Diagnostic: log every panel's type/visibility/bounds so we can verify the
        // UI sends changing Main Panel dimensions during a resize/move.
        TM_INFO("  panel[%d] type=%d vis=%d x=%.3f y=%.3f w=%.3f h=%.3f z=%d",
                i, newPanel.panelType, newPanel.visible,
                newPanel.x, newPanel.y, newPanel.w, newPanel.h, newPanel.zOrder);

        m_overlayPanels[i] = newPanel;
        
        // Initialize new fields for existing panels from UI that don't set them
        if (m_overlayPanels[i].fontWeight < 0 || m_overlayPanels[i].fontWeight > 3) m_overlayPanels[i].fontWeight = 1;
        if (m_overlayPanels[i].fontStyle < 0 || m_overlayPanels[i].fontStyle > 2) m_overlayPanels[i].fontStyle = 0;
        if (m_overlayPanels[i].bgStyle < 0 || m_overlayPanels[i].bgStyle > 3) m_overlayPanels[i].bgStyle = 0;
        if (m_overlayPanels[i].layoutMode < 0 || m_overlayPanels[i].layoutMode > 3) m_overlayPanels[i].layoutMode = 1;
        if (m_overlayPanels[i].textAnimMode < 0 || m_overlayPanels[i].textAnimMode > 5) m_overlayPanels[i].textAnimMode = 0;
        if (m_overlayPanels[i].scrollSpeed <= 0) m_overlayPanels[i].scrollSpeed = 300.0f;
        if (!m_overlayPanels[i].fontName[0]) wcscpy_s(m_overlayPanels[i].fontName, L"Segoe UI");
        if (m_overlayPanels[i].textColor2 == 0) m_overlayPanels[i].textColor2 = m_overlayPanels[i].textColor ? m_overlayPanels[i].textColor : RGB(255, 255, 255);
        if (!m_overlayPanels[i].headingFontName[0]) wcscpy_s(m_overlayPanels[i].headingFontName, m_overlayPanels[i].fontName);
        if (!m_overlayPanels[i].songFontName[0]) wcscpy_s(m_overlayPanels[i].songFontName, m_overlayPanels[i].fontName);
        if (m_overlayPanels[i].headingTextColor == 0) m_overlayPanels[i].headingTextColor = m_overlayPanels[i].textColor ? m_overlayPanels[i].textColor : RGB(255, 255, 255);
        if (m_overlayPanels[i].headingTextColor2 == 0) m_overlayPanels[i].headingTextColor2 = m_overlayPanels[i].headingTextColor;
        if (m_overlayPanels[i].songTextColor == 0) m_overlayPanels[i].songTextColor = RGB(255, 255, 255);
        if (m_overlayPanels[i].songTextColor2 == 0) m_overlayPanels[i].songTextColor2 = m_overlayPanels[i].songTextColor;
        if (!m_overlayPanels[i].showTitle && !m_overlayPanels[i].showArtist) {
            m_overlayPanels[i].showTitle = true;
            m_overlayPanels[i].showArtist = true;
        }

        // Rebuild the cached image only when the image panel content actually changed.
        if (newPanel.panelType == 1 && newPanel.hasImage && newPanel.imagePath[0]) {
            if (!sameImage) {
                if (m_overlayImageSRVs[i]) { m_overlayImageSRVs[i]->Release(); m_overlayImageSRVs[i] = nullptr; }
                if (m_overlayImages[i]) { delete m_overlayImages[i]; m_overlayImages[i] = nullptr; }
                m_overlayImagePixels[i].clear();
                m_overlayImagePixels[i].shrink_to_fit();
                m_overlayImagePixelW[i] = 0;
                m_overlayImagePixelH[i] = 0;

                Gdiplus::Image* img = Gdiplus::Image::FromFile(newPanel.imagePath);
                if (img && img->GetLastStatus() == Gdiplus::Ok) {
                    m_overlayImages[i] = img;
                    if (m_d3dReady && m_d3dDevice) {
                        if (!CreateTextureFromImage(m_d3dDevice, img, &m_overlayImageSRVs[i])) {
                            TM_WARN("UISetOverlayPanels: failed to build GPU texture for panel %d: %S", i, newPanel.imagePath);
                        }
                    }
                } else {
                    if (img) delete img;
                    m_overlayPanels[i].hasImage = false;
                    TM_WARN("UISetOverlayPanels: failed to load image for panel %d: %S",
                            i, newPanel.imagePath);
                }
            }
        } else {
            if (m_overlayImageSRVs[i]) { m_overlayImageSRVs[i]->Release(); m_overlayImageSRVs[i] = nullptr; }
            if (m_overlayImages[i]) { delete m_overlayImages[i]; m_overlayImages[i] = nullptr; }
            m_overlayImagePixels[i].clear();
            m_overlayImagePixels[i].shrink_to_fit();
            m_overlayImagePixelW[i] = 0;
            m_overlayImagePixelH[i] = 0;
        }
    }
    TM_INFO("UISetOverlayPanels: received %d panels", count);
    LeaveCriticalSection(&m_overlayLock);

    // Build GPU textures for all panels (images, text, song)
    if (m_d3dReady && m_d3dDevice) {
        BuildOverlayPanelTextures();
    }
}

// ─── Render overlay panels to buffer (GDI+) ─────────────────────────────────────
void TmPlugin::RenderPanelsToBuffer(const OverlayPanel* panels, Gdiplus::Image** images,
                                     ID3D11ShaderResourceView* const* panelSrvs,
                                     int count, BYTE* buffer, int bufW, int bufH, bool skipGpuImagePanels,
                                     const BYTE* slideshowFrame, int ssFrameW, int ssFrameH)
{
    if (!panels || !buffer || bufW <= 0 || bufH <= 0) return;

    // Clear buffer to transparent (BGRA: 0,0,0,0)
    memset(buffer, 0, bufW * bufH * 4);

    // Create GDI+ bitmap from buffer
    Gdiplus::Bitmap bmp(bufW, bufH, bufW * 4, PixelFormat32bppARGB, buffer);
    Gdiplus::Graphics graphics(&bmp);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    int sorted[MAX_OVERLAY_PANELS];
    for (int i = 0; i < count; ++i) sorted[i] = i;
    for (int i = 0; i < count - 1; ++i)
        for (int j = i + 1; j < count; ++j)
            if (panels[sorted[i]].zOrder > panels[sorted[j]].zOrder)
                { int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }

    for (int i = 0; i < count; ++i) {
        int idx = sorted[i];
        const OverlayPanel& p = panels[idx];
        if (!p.visible) continue;
        if (p.panelType == 0) {
            // Main panel: blit slideshow frame at this z-order position if available
            if (slideshowFrame && ssFrameW > 0 && ssFrameH > 0) {
                int pxOff = (int)(p.x * bufW);
                int pyOff = (int)(p.y * bufH);
                for (int row = 0; row < ssFrameH; ++row) {
                    int dstY = pyOff + row;
                    if (dstY < 0 || dstY >= bufH) continue;
                    int dstX = pxOff;
                    int copyW = ssFrameW;
                    if (dstX < 0) { copyW += dstX; dstX = 0; }
                    if (dstX + copyW > bufW) copyW = bufW - dstX;
                    if (copyW <= 0) continue;
                    memcpy(buffer + (dstY * bufW + dstX) * 4,
                           slideshowFrame + row * ssFrameW * 4,
                           copyW * 4);
                }
            }
            continue;
        }
        if (skipGpuImagePanels && p.panelType == 1 && panelSrvs && panelSrvs[idx]) continue;

        int px = (int)(p.x * bufW);
        int py = (int)(p.y * bufH);
        int pw = (int)(p.w * bufW);
        int ph = (int)(p.h * bufH);
        if (pw <= 0 || ph <= 0) continue;

        // Image panel
        if (p.panelType == 1 && p.hasImage && images[idx]) {
            int mode = p.imageAnimMode;
            if (mode == 0) {
                const bool cacheValid = (m_overlayImagePixelW[idx] == pw && m_overlayImagePixelH[idx] == ph &&
                                         !m_overlayImagePixels[idx].empty());
                if (!cacheValid) {
                    if (BuildScaledImagePixels(images[idx], pw, ph, m_overlayImagePixels[idx])) {
                        m_overlayImagePixelW[idx] = pw;
                        m_overlayImagePixelH[idx] = ph;
                    }
                }

                if (m_overlayImagePixelW[idx] == pw && m_overlayImagePixelH[idx] == ph && !m_overlayImagePixels[idx].empty()) {
                    BYTE* dstBase = buffer + py * bufW * 4 + px * 4;
                    const BYTE* srcBase = m_overlayImagePixels[idx].data();
                    for (int row = 0; row < ph; ++row) {
                        BYTE* dstRow = dstBase + (size_t)row * bufW * 4;
                        const BYTE* srcRow = srcBase + (size_t)row * pw * 4;
                        for (int col = 0; col < pw; ++col) {
                            const BYTE sb = srcRow[col * 4 + 0];
                            const BYTE sg = srcRow[col * 4 + 1];
                            const BYTE sr = srcRow[col * 4 + 2];
                            const BYTE sa = srcRow[col * 4 + 3];
                            if (sa == 0) continue;
                            BYTE* dstPx = dstRow + col * 4;
                            if (sa == 255) {
                                dstPx[0] = sb;
                                dstPx[1] = sg;
                                dstPx[2] = sr;
                                dstPx[3] = 255;
                            } else {
                                dstPx[0] = BlendChannel(sb, dstPx[0], sa);
                                dstPx[1] = BlendChannel(sg, dstPx[1], sa);
                                dstPx[2] = BlendChannel(sr, dstPx[2], sa);
                                dstPx[3] = (BYTE)(sa + (int)dstPx[3] * (255 - sa) / 255);
                            }
                        }
                    }
                } else {
                    Gdiplus::Image* img = images[idx];
                    if (img && img->GetLastStatus() == Gdiplus::Ok) {
                        graphics.DrawImage(img, px, py, pw, ph);
                    }
                }
            } else {
                static LARGE_INTEGER sFreq = {};
                static LARGE_INTEGER sStart = {};
                static bool sInit = false;
                if (!sInit) { QueryPerformanceFrequency(&sFreq); QueryPerformanceCounter(&sStart); sInit = true; }
                LARGE_INTEGER now = {};
                QueryPerformanceCounter(&now);
                float t = (float)(now.QuadPart - sStart.QuadPart) / (float)sFreq.QuadPart;

                Gdiplus::Image* img = images[idx];
                if (!img || img->GetLastStatus() != Gdiplus::Ok) continue;

                Gdiplus::GraphicsState st = graphics.Save();
                Gdiplus::Rect clip(px, py, pw, ph);
                graphics.SetClip(clip);

                Gdiplus::REAL iw = (Gdiplus::REAL)img->GetWidth();
                Gdiplus::REAL ih = (Gdiplus::REAL)img->GetHeight();
                if (iw <= 0 || ih <= 0) { graphics.Restore(st); continue; }

                float s0 = (float)min((double)pw / (double)iw, (double)ph / (double)ih);
                float deg = 0.0f;
                if (mode == 1) deg = sinf(t * 0.6f) * 10.0f;
                else if (mode == 2) deg = fmodf(t * 30.0f, 360.0f);

                float rad = deg * 3.14159265f / 180.0f;
                float c = fabsf(cosf(rad));
                float s = fabsf(sinf(rad));
                float dw0 = iw * s0;
                float dh0 = ih * s0;
                float fit = 1.0f;
                if (mode == 1 || mode == 2) {
                    float bbW = dw0 * c + dh0 * s;
                    float bbH = dw0 * s + dh0 * c;
                    float sx = (bbW > 0) ? (float)pw / bbW : 1.0f;
                    float sy = (bbH > 0) ? (float)ph / bbH : 1.0f;
                    fit = min(sx, sy) * 0.98f;
                } else if (mode == 3 || mode == 4) {
                    fit = 0.92f;
                }
                float dw = dw0 * fit;
                float dh = dh0 * fit;

                if (mode == 4) {
                    float mx = max(0.0f, (pw - dw) * 0.5f);
                    float my = max(0.0f, (ph - dh) * 0.5f);
                    float ax = sinf(t * 1.3f) * (mx * 0.6f);
                    float bx = sinf(t * 1.7f + 1.0f) * (mx * 0.6f);
                    float ay = cosf(t * 1.2f) * (my * 0.3f);
                    float by = cosf(t * 1.5f + 0.8f) * (my * 0.3f);
                    float bx0 = px + mx;
                    float by0 = py + my;
                    Gdiplus::PointF pts[3] = {
                        Gdiplus::PointF((Gdiplus::REAL)(bx0 + ax),            (Gdiplus::REAL)(by0 + ay)),
                        Gdiplus::PointF((Gdiplus::REAL)(bx0 + dw + bx),       (Gdiplus::REAL)(by0 - ay)),
                        Gdiplus::PointF((Gdiplus::REAL)(bx0 - bx),            (Gdiplus::REAL)(by0 + dh + by))
                    };
                    graphics.DrawImage(img, pts, 3);
                } else {
                    float cx = px + pw * 0.5f;
                    float cy = py + ph * 0.5f;
                    graphics.TranslateTransform((Gdiplus::REAL)cx, (Gdiplus::REAL)cy);
                    if (mode == 1 || mode == 2) {
                        graphics.RotateTransform((Gdiplus::REAL)deg);
                    } else if (mode == 3) {
                        float rx = max(0.0f, (pw - dw) * 0.5f) * 0.9f;
                        float ry = max(0.0f, (ph - dh) * 0.5f) * 0.9f;
                        float dx = cosf(t * 0.8f) * rx;
                        float dy = sinf(t * 0.6f) * ry;
                        graphics.TranslateTransform((Gdiplus::REAL)dx, (Gdiplus::REAL)dy);
                    }
                    graphics.DrawImage(img, (Gdiplus::REAL)(-dw * 0.5f), (Gdiplus::REAL)(-dh * 0.5f), (Gdiplus::REAL)dw, (Gdiplus::REAL)dh);
                }

                graphics.Restore(st);
            }
        }
        // Shader panel - render the shader into a CPU BGRA buffer, then composite it
        else if (p.panelType == 3) {
            int shaderIndex = p.shaderIndex;
            int shaderCount = m_renderer.GetShaderCount();
            if (shaderIndex < 0 || shaderIndex >= shaderCount) shaderIndex = m_mainShaderIndex;
            if (shaderIndex < 0 || shaderIndex >= shaderCount) shaderIndex = 0;

            TmRenderer::ShaderUniforms uniforms = {};
            static LARGE_INTEGER shaderStart = {};
            static bool shaderStartInit = false;
            static LARGE_INTEGER shaderFreq = {};
            if (!shaderStartInit) {
                QueryPerformanceFrequency(&shaderFreq);
                QueryPerformanceCounter(&shaderStart);
                shaderStartInit = true;
            }
            LARGE_INTEGER shaderNow = {};
            QueryPerformanceCounter(&shaderNow);
            uniforms.iTime = (float)(shaderNow.QuadPart - shaderStart.QuadPart) / (float)shaderFreq.QuadPart;
            EnterCriticalSection(&m_audioLock);
            uniforms.iBeat = m_audio.beat;
            uniforms.iLevel = m_audio.level;
            uniforms.iBass = m_audio.bass;
            uniforms.iMid = m_audio.mid;
            uniforms.iTreble = m_audio.treble;
            uniforms.iBpm = SongBpm > 0 ? (float)SongBpm : 120.0f;
            uniforms.iSongPosBeats = (float)SongPosBeats;
            LeaveCriticalSection(&m_audioLock);
            uniforms.iResolution[0] = (float)pw;
            uniforms.iResolution[1] = (float)ph;
            uniforms._pad = 0.0f;

            std::vector<BYTE> shaderPixels;
            if (m_renderer.RenderShaderToPixels(shaderIndex, pw, ph, uniforms, shaderPixels) && !shaderPixels.empty()) {
                Gdiplus::Bitmap shaderBmp(pw, ph, pw * 4, PixelFormat32bppARGB, shaderPixels.data());
                graphics.DrawImage(&shaderBmp, px, py, pw, ph);
            }
        }
        // Text panel
        else if (p.panelType == 2 && p.textContent[0]) {
            // Background color if set
            if (p.hasBgColor) {
                Gdiplus::Color bgColor((BYTE)p.bgOpacity,
                    GetRValue(p.bgColor), GetGValue(p.bgColor), GetBValue(p.bgColor));
                Gdiplus::SolidBrush bg(bgColor);
                graphics.FillRectangle(&bg, px, py, pw, ph);
            }

            // Text color
            Gdiplus::Color textColor = ColorFromRgbAndAlpha(p.textColor, p.textAlpha ? p.textAlpha : 255);
            Gdiplus::Color textColor2 = ColorFromRgbAndAlpha(p.textColor2, p.textAlpha2 ? p.textAlpha2 : (p.textAlpha ? p.textAlpha : 255));

            // Font
            Gdiplus::FontFamily fontFamily(p.fontName[0] ? p.fontName : L"Arial");
            Gdiplus::Font font(&fontFamily, (Gdiplus::REAL)p.fontSize,
                Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

            // Text layout (simple for now - centered in panel)
            Gdiplus::RectF rect((Gdiplus::REAL)px, (Gdiplus::REAL)py,
                                (Gdiplus::REAL)pw, (Gdiplus::REAL)ph);
            DrawAnimatedTextPanel(graphics, p, rect, textColor, textColor2);
        }
        // Song / Now Playing panel
        else if (p.panelType == 4) {
            // ── Clip to panel bounds — text NEVER draws outside this rect ─────────────
            Gdiplus::Rect clipRect(px, py, pw, ph);
            graphics.SetClip(clipRect);

            // ── Background ───────────────────────────────────────────────────────────
            if (p.hasBgColor) {
                if (p.bgStyle == 1) {
                    Gdiplus::Color c1((BYTE)p.bgOpacity, GetRValue(p.bgColor), GetGValue(p.bgColor), GetBValue(p.bgColor));
                    Gdiplus::Color c2((BYTE)p.bgOpacity, GetRValue(p.bgColor2), GetGValue(p.bgColor2), GetBValue(p.bgColor2));
                    Gdiplus::Rect gr(px, py, pw, ph);
                    Gdiplus::LinearGradientBrush lgb(gr, c1, c2, Gdiplus::LinearGradientModeVertical);
                    graphics.FillRectangle(&lgb, px, py, pw, ph);
                } else {
                    Gdiplus::Color bgColor((BYTE)p.bgOpacity, GetRValue(p.bgColor), GetGValue(p.bgColor), GetBValue(p.bgColor));
                    Gdiplus::SolidBrush bg(bgColor);
                    graphics.FillRectangle(&bg, px, py, pw, ph);
                }
                if (p.shadowBlur > 0) {
                    Gdiplus::Color sc(128, GetRValue(p.shadowColor), GetGValue(p.shadowColor), GetBValue(p.shadowColor));
                    Gdiplus::SolidBrush sb(sc);
                    int so = p.shadowBlur / 2;
                    graphics.FillRectangle(&sb, px + so, py + so, pw, ph);
                }
            }

            // ── Track info ───────────────────────────────────────────────────────────
            RefreshNowPlayingCache();
            wchar_t titleText[256] = {};
            wchar_t artistText[256] = {};
            wchar_t albumText[256] = {};
            wchar_t bpmText[32] = {};
            wchar_t timeText[32] = {};
            if (m_songDisplay[0]) {
                wchar_t* dash = wcschr(m_songDisplay, L'-');
                if (dash) {
                    *dash = L'\0';
                    wcscpy_s(artistText, m_songDisplay);
                    wcscpy_s(titleText, dash + 1);
                    *dash = L'-';
                } else {
                    wcscpy_s(titleText, m_songDisplay);
                }
            }

            // ── Rainbow / color ──────────────────────────────────────────────────────
            static DWORD rainbowTickMember = 0;
            rainbowTickMember++;
            float hue = (float)(rainbowTickMember % 360) / 360.0f;

            // ── Time base for scroll animation ───────────────────────────────────────
            static LARGE_INTEGER scrollAnimStart = {};
            static LARGE_INTEGER scrollAnimFreq = {};
            static bool scrollAnimInit = false;
            if (!scrollAnimInit) {
                QueryPerformanceFrequency(&scrollAnimFreq);
                QueryPerformanceCounter(&scrollAnimStart);
                scrollAnimInit = true;
            }
            LARGE_INTEGER scrollAnimNow = {};
            QueryPerformanceCounter(&scrollAnimNow);
            float scrollAnimElapsed = (float)(scrollAnimNow.QuadPart - scrollAnimStart.QuadPart) / (float)scrollAnimFreq.QuadPart;

            // ── Colors ───────────────────────────────────────────────────────────────
            Gdiplus::Color headingColor = ColorFromRgbAndAlpha(p.headingTextColor, p.headingTextAlpha ? p.headingTextAlpha : 255);
            if (p.rainbowColor) headingColor = HSVtoRGB(hue, 1.0f, 1.0f);
            Gdiplus::Color headingColor2 = ColorFromRgbAndAlpha(p.headingTextColor2, p.headingTextAlpha2 ? p.headingTextAlpha2 : (p.headingTextAlpha ? p.headingTextAlpha : 255));
            if (p.rainbowColor) headingColor2 = headingColor;

            Gdiplus::Color songColor = ColorFromRgbAndAlpha(p.songTextColor, p.songTextAlpha ? p.songTextAlpha : 255);
            Gdiplus::Color songColor2 = ColorFromRgbAndAlpha(p.songTextColor2, p.songTextAlpha2 ? p.songTextAlpha2 : (p.songTextAlpha ? p.songTextAlpha : 255));

            // ── Fonts ────────────────────────────────────────────────────────────────
            Gdiplus::FontStyle fontStyle = Gdiplus::FontStyleRegular;
            if (p.fontWeight == 1) fontStyle = Gdiplus::FontStyleBold;
            if (p.fontStyle == 1) fontStyle = (Gdiplus::FontStyle)(fontStyle | Gdiplus::FontStyleItalic);
            Gdiplus::FontFamily headingFamily(p.headingFontName[0] ? p.headingFontName : (p.fontName[0] ? p.fontName : L"Segoe UI"));
            Gdiplus::FontFamily songFamily(p.songFontName[0] ? p.songFontName : (p.fontName[0] ? p.fontName : L"Segoe UI"));
            Gdiplus::Font titleFont(&headingFamily, (Gdiplus::REAL)p.fontSize, fontStyle, Gdiplus::UnitPixel);
            Gdiplus::Font bodyFont(&songFamily, (Gdiplus::REAL)max(16, p.fontSize - 6), fontStyle, Gdiplus::UnitPixel);
            Gdiplus::Font smallFont(&songFamily, (Gdiplus::REAL)max(12, p.fontSize - 10), fontStyle, Gdiplus::UnitPixel);

            // ── Layout constants (panel-relative, never animated) ─────────────────────
            const int padX = 12, padY = 10;
            const int innerX = px + padX;          // left edge of text area
            const int innerW = pw - padX * 2;      // available text width
            const int innerY = py + padY;           // top edge of text area
            const int lineH  = max(20, p.fontSize + 4);
            const int headH  = max(40, p.fontSize + 16);

            // ── Primary song text ────────────────────────────────────────────────────
            const wchar_t* songText = m_songDisplay[0] ? m_songDisplay : L"No Track Playing";
            Gdiplus::RectF measureLayout(0.0f, 0.0f, 100000.0f, 10000.0f);
            Gdiplus::RectF songBounds;
            graphics.MeasureString(songText, -1, &bodyFont, measureLayout, &songBounds);
            float songTextW = max(1.0f, songBounds.Width);

            // ── Animation offset — applied to SONG TEXT RECT X only ───────────────────
            // The clip region (px,py,pw,ph) ensures nothing renders outside the panel.
            float animOffsetX = 0.0f;
            bool scrollNoEllipsis = false;

            if (p.songAnimMode == 1) { // Scroll Left to Right
                float speed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                float totalDist = (float)innerW + songTextW;
                float duration = totalDist / speed;
                if (duration < 0.001f) duration = 0.001f;
                float progress = fmodf(scrollAnimElapsed, duration) / duration;
                animOffsetX = -songTextW + progress * totalDist;
                scrollNoEllipsis = true;
            } else if (p.songAnimMode == 2) { // Scroll Right to Left
                float speed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                float totalDist = (float)innerW + songTextW;
                float duration = totalDist / speed;
                if (duration < 0.001f) duration = 0.001f;
                float progress = fmodf(scrollAnimElapsed, duration) / duration;
                animOffsetX = (float)innerW - progress * totalDist;
                scrollNoEllipsis = true;
            } else if (p.songAnimMode == 3) { // Bounce left↔right within panel
                float speed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                float maxTravel;
                float direction;
                if (songTextW <= (float)innerW) {
                    // Text fits: bounce rightward within the available space
                    maxTravel = (float)innerW - songTextW;
                    direction = 1.0f;
                } else {
                    // Text overflows: scroll leftward to reveal the end
                    maxTravel = songTextW - (float)innerW;
                    direction = -1.0f;
                    scrollNoEllipsis = true;
                }
                if (maxTravel > 0.0f) {
                    float duration = (maxTravel * 2.0f) / speed;
                    if (duration < 0.001f) duration = 0.001f;
                    float t = fmodf(scrollAnimElapsed, duration) / duration;
                    float pos = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
                    animOffsetX = direction * pos * maxTravel;
                }
            }

            // ── String formats ───────────────────────────────────────────────────────
            // Heading: always static (fixed at panel left), ellipsis trims if needed
            Gdiplus::StringFormat headFmt;
            headFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            headFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            headFmt.SetAlignment(Gdiplus::StringAlignmentNear);
            headFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

            // Song: animated; no ellipsis when scrolling so the full text renders
            Gdiplus::StringFormat songFmt;
            songFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            songFmt.SetTrimming(scrollNoEllipsis ? Gdiplus::StringTrimmingNone : Gdiplus::StringTrimmingEllipsisCharacter);
            if (p.textAlign == 1) {
                songFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            } else if (p.textAlign == 2) {
                songFmt.SetAlignment(Gdiplus::StringAlignmentFar);
            } else {
                songFmt.SetAlignment(Gdiplus::StringAlignmentNear);
            }
            songFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

            // ── Song rect: X is animated; width is full text for scroll modes ─────────
            float songRectX = (float)innerX + animOffsetX;
            float songRectW = scrollNoEllipsis ? max(songTextW + 4.0f, (float)innerW) : (float)innerW;

            // ── Render by layout mode ────────────────────────────────────────────────
            if (p.layoutMode == 0 || p.layoutMode == 1) {
                // Single combined line: "Heading Song" — animated together
                wchar_t heading[128];
                swprintf_s(heading, L"%s", p.textContent[0] ? p.textContent : L"Now Playing:");
                wchar_t liveText[640];
                swprintf_s(liveText, L"%s %s", heading, songText);

                Gdiplus::RectF liveBounds;
                graphics.MeasureString(liveText, -1, &titleFont, measureLayout, &liveBounds);
                float liveTextW = max(1.0f, liveBounds.Width);

                float liveAnimOffsetX = 0.0f;
                bool liveNoEllipsis = false;
                if (p.songAnimMode == 1) {
                    float speed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                    float totalDist = (float)innerW + liveTextW;
                    float duration = totalDist / speed;
                    if (duration < 0.001f) duration = 0.001f;
                    float progress = fmodf(scrollAnimElapsed, duration) / duration;
                    liveAnimOffsetX = -liveTextW + progress * totalDist;
                    liveNoEllipsis = true;
                } else if (p.songAnimMode == 2) {
                    float speed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                    float totalDist = (float)innerW + liveTextW;
                    float duration = totalDist / speed;
                    if (duration < 0.001f) duration = 0.001f;
                    float progress = fmodf(scrollAnimElapsed, duration) / duration;
                    liveAnimOffsetX = (float)innerW - progress * totalDist;
                    liveNoEllipsis = true;
                } else if (p.songAnimMode == 3) {
                    float speed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                    float liveMaxTravel;
                    float liveDir;
                    if (liveTextW <= (float)innerW) {
                        liveMaxTravel = (float)innerW - liveTextW;
                        liveDir = 1.0f;
                    } else {
                        liveMaxTravel = liveTextW - (float)innerW;
                        liveDir = -1.0f;
                        liveNoEllipsis = true;
                    }
                    if (liveMaxTravel > 0.0f) {
                        float duration = (liveMaxTravel * 2.0f) / speed;
                        if (duration < 0.001f) duration = 0.001f;
                        float t = fmodf(scrollAnimElapsed, duration) / duration;
                        float pos = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
                        liveAnimOffsetX = liveDir * pos * liveMaxTravel;
                    }
                }

                Gdiplus::StringFormat liveFmt;
                liveFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                liveFmt.SetTrimming(liveNoEllipsis ? Gdiplus::StringTrimmingNone : Gdiplus::StringTrimmingEllipsisCharacter);
                liveFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                liveFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

                float liveRectX = (float)innerX + liveAnimOffsetX;
                float liveRectW = liveNoEllipsis ? max(liveTextW + 4.0f, (float)innerW) : (float)innerW;
                Gdiplus::RectF liveRect(liveRectX, (Gdiplus::REAL)innerY, liveRectW, (Gdiplus::REAL)headH);
                DrawTextWithOptionalGradient(graphics, liveText, titleFont, liveRect, liveFmt, headingColor, headingColor2);

            } else if (p.layoutMode == 2) {
                // Compact: title + optional BPM/time
                int textY = innerY;
                if (p.showTitle && titleText[0]) {
                    Gdiplus::RectF titleRect(songRectX, (Gdiplus::REAL)textY, songRectW, (Gdiplus::REAL)lineH);
                    DrawTextWithOptionalGradient(graphics, titleText, titleFont, titleRect, songFmt, headingColor, headingColor2);
                    textY += lineH;
                }
                if ((p.showBPM && bpmText[0]) || (p.showTime && timeText[0])) {
                    wchar_t infoText[64] = {};
                    if (bpmText[0] && timeText[0]) swprintf_s(infoText, L"%s | %s", bpmText, timeText);
                    else if (bpmText[0]) wcscpy_s(infoText, bpmText);
                    else wcscpy_s(infoText, timeText);
                    Gdiplus::RectF infoRect((Gdiplus::REAL)innerX, (Gdiplus::REAL)textY, (Gdiplus::REAL)innerW, (Gdiplus::REAL)lineH);
                    DrawTextWithOptionalGradient(graphics, infoText, smallFont, infoRect, headFmt, songColor, songColor2);
                }
            } else if (p.layoutMode == 3) {
                // Full: title + artist + album + BPM/time
                int textY = innerY;
                if (p.showTitle && titleText[0]) {
                    Gdiplus::RectF titleRect(songRectX, (Gdiplus::REAL)textY, songRectW, (Gdiplus::REAL)lineH);
                    DrawTextWithOptionalGradient(graphics, titleText, titleFont, titleRect, songFmt, headingColor, headingColor2);
                    textY += lineH;
                }
                if (p.showArtist && artistText[0]) {
                    Gdiplus::RectF artistRect(songRectX, (Gdiplus::REAL)textY, songRectW, (Gdiplus::REAL)lineH);
                    DrawTextWithOptionalGradient(graphics, artistText, bodyFont, artistRect, songFmt, songColor, songColor2);
                    textY += lineH;
                }
                if (p.showAlbum && albumText[0]) {
                    Gdiplus::RectF albumRect(songRectX, (Gdiplus::REAL)textY, songRectW, (Gdiplus::REAL)lineH);
                    DrawTextWithOptionalGradient(graphics, albumText, bodyFont, albumRect, songFmt, songColor, songColor2);
                    textY += lineH;
                }
                if ((p.showBPM && bpmText[0]) || (p.showTime && timeText[0])) {
                    wchar_t infoText[64] = {};
                    if (bpmText[0] && timeText[0]) swprintf_s(infoText, L"%s | %s", bpmText, timeText);
                    else if (bpmText[0]) wcscpy_s(infoText, bpmText);
                    else wcscpy_s(infoText, timeText);
                    Gdiplus::RectF infoRect((Gdiplus::REAL)innerX, (Gdiplus::REAL)textY, (Gdiplus::REAL)innerW, (Gdiplus::REAL)lineH);
                    DrawTextWithOptionalGradient(graphics, infoText, smallFont, infoRect, headFmt, songColor, songColor2);
                }
            } else {
                // Default/fallback: static heading label + animated song text below
                wchar_t heading[128];
                swprintf_s(heading, L"%s", p.textContent[0] ? p.textContent : L"Now Playing:");
                Gdiplus::RectF headingRect((Gdiplus::REAL)innerX, (Gdiplus::REAL)innerY, (Gdiplus::REAL)innerW, (Gdiplus::REAL)headH);
                DrawTextWithOptionalGradient(graphics, heading, titleFont, headingRect, headFmt, headingColor, headingColor2);

                int songTop = innerY + headH + padY;
                int songH = ph - (songTop - py) - padY;
                if (songH > 0) {
                    Gdiplus::RectF songRectF(songRectX, (Gdiplus::REAL)songTop, songRectW, (Gdiplus::REAL)songH);
                    DrawTextWithOptionalGradient(graphics, songText, bodyFont, songRectF, songFmt, songColor, songColor2);
                }
            }

            // ── Legacy combined scroll mode (songAnimMode == 7) ───────────────────────
            if (p.songAnimMode == 7) {
                wchar_t combHeading[128];
                swprintf_s(combHeading, L"%s", p.textContent[0] ? p.textContent : L"Now Playing:");
                wchar_t combinedText[640];
                swprintf_s(combinedText, L"%s %s", combHeading, songText);

                Gdiplus::RectF combinedBounds;
                graphics.MeasureString(combinedText, -1, &titleFont, measureLayout, &combinedBounds);
                float combinedW = max(1.0f, combinedBounds.Width + 4.0f);

                float combSpeed = p.scrollSpeed > 0 ? (float)p.scrollSpeed : 200.0f;
                float maxTravel = max(0.0f, combinedW - (float)innerW);
                float combOffsetX = 0.0f;
                bool combNoEllipsis = false;
                if (maxTravel > 0.0f) {
                    float duration = (maxTravel * 2.0f) / combSpeed;
                    if (duration < 0.001f) duration = 0.001f;
                    float t = fmodf(scrollAnimElapsed, duration) / duration;
                    float pos = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
                    combOffsetX = -pos * maxTravel;
                    combNoEllipsis = true;
                }

                Gdiplus::StringFormat combFmt;
                combFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                combFmt.SetTrimming(combNoEllipsis ? Gdiplus::StringTrimmingNone : Gdiplus::StringTrimmingEllipsisCharacter);
                combFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                combFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

                float combRectX = (float)innerX + combOffsetX;
                float combRectW = max(combinedW, (float)innerW);
                Gdiplus::RectF combRect(combRectX, (Gdiplus::REAL)innerY, combRectW, (Gdiplus::REAL)headH);
                DrawTextWithOptionalGradient(graphics, combinedText, titleFont, combRect, combFmt, headingColor, headingColor2);
            }

            // ── Reset clip so it doesn't affect other panels ──────────────────────────
            graphics.ResetClip();
        }
    }

    // If no Main Panel exists but we have a slideshow frame, render it fullscreen
    bool hasMainPanel = false;
    for (int i = 0; i < count; ++i) {
        if (panels[i].panelType == 0 && panels[i].visible) {
            hasMainPanel = true;
            break;
        }
    }
    if (!hasMainPanel && slideshowFrame && ssFrameW > 0 && ssFrameH > 0) {
        // Scale slideshow frame to fit buffer
        int copyW = min(ssFrameW, bufW);
        int copyH = min(ssFrameH, bufH);
        for (int row = 0; row < copyH; ++row) {
            memcpy(buffer + row * bufW * 4,
                   slideshowFrame + row * ssFrameW * 4,
                   copyW * 4);
        }
    }
}
