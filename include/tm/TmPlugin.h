#pragma once
#include "vdj/vdjVideo8.h"
#include "tm/TmUI.h"
#include "tm/TmTypes.h"
#include "tm/TmServices.h"
#include "tm/TmRenderer.h"
#include "tm/TmMedia.h"
#include <vector>

#if defined(VDJ_WIN)
#include <d3d11.h>
namespace Gdiplus { class Image; }
#elif defined(VDJ_MAC)
// macOS: no GDI+ or D3D11. Overlay images use CoreGraphics.
// CGImageRef is defined by CoreGraphics; provide fallback for pure C++.
#ifndef __OBJC__
typedef void* CGImageRef;
#endif
#endif

// TellyMedia v4 VirtualDJ Video FX plugin.
// Phase 1: loads in VDJ and presents the banks/grid/sidebar settings UI.
// Rendering to the deck (TmRenderer/TmMedia) is wired in Phase 2.
class TmPlugin : public IVdjPluginVideoFx8, public ITmUIHost
{
public:
    // Note: hInstance is inherited from IVdjPlugin8 and set by VirtualDJ.
    TmPlugin();
    virtual ~TmPlugin();

    // ─── IVdjPlugin8 ─────────────────────────────────────────────────────────
    HRESULT VDJ_API OnLoad() override;
    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override;
    ULONG   VDJ_API Release() override;
    HRESULT VDJ_API OnParameter(int id) override;
    HRESULT VDJ_API OnGetUserInterface(TVdjPluginInterface8* pi) override;

    // ─── IVdjPluginVideoFx8 ──────────────────────────────────────────────────
    HRESULT VDJ_API OnStart() override;
    HRESULT VDJ_API OnStop() override;
    HRESULT VDJ_API OnDraw() override;
    HRESULT VDJ_API OnDeviceInit() override;
    HRESULT VDJ_API OnDeviceClose() override;
    HRESULT VDJ_API OnAudioSamples(float* buf, int nb) override;

    // ─── ITmUIHost (UI -> plugin actions) ────────────────────────────────────
    void HostPlayMedia(const wchar_t* path, int scaleMode) override;
    void HostStop() override;
    void HostPause() override;
    void HostScaleMode(int mode) override;
    void HostTransitionMode(int mode) override;
    void HostSlideshowStart() override;
    void HostSlideshowStop() override;
    void HostRenderMode(int mode) override;
    void HostGetNowPlayingText(wchar_t* outText, int outTextMax) override;
    void HostGetRenderModeText(wchar_t* outText, int outTextMax) override;
    void HostSetShadersEnabled(bool enabled) override;
    void HostSetShaderKaraokeDisable(bool disable) override;
    void HostSetKaraokeAutoHide(bool enabled) override;
    void HostSetMainShader(int index) override;
    void HostReloadShaders() override;
    void HostGetShaderNames(wchar_t* outNames, int outNamesMax) override;
    bool HostGetLicenseState(LicenseState* outState) override;
    bool HostLicenseLogin(const wchar_t* email, const wchar_t* password, bool savePassword) override;
    bool HostLicenseValidate() override;
    bool HostLicenseActivate() override;
    void HostLicenseLogout() override;

    // ─── Overlay panel management ─────────────────────────────────────────────
    void UISetOverlayPanels(const OverlayPanel* panels, int count);

private:
    // Refresh cached now-playing metadata from VirtualDJ.
    void RefreshNowPlayingCache();

    // Free all cached overlay images (and null the pointers).
    void ClearOverlayImageCache();
    void ReleaseOverlayPanelTextures();
    void BuildOverlayPanelTextures();
    float EstimateOverlayImageLoad() const;
    void DrawOverlayImagePanels();
    
    // Render overlay panels (text/images) into a BGRA buffer for GPU upload.
    void RenderPanelsToBuffer(const OverlayPanel* panels,
#if defined(VDJ_WIN)
                               Gdiplus::Image** images,
                               ID3D11ShaderResourceView* const* panelSrvs,
#elif defined(VDJ_MAC)
                               CGImageRef* images,
                               GLuint const* panelTexs,
#endif
                               int count, BYTE* buffer, int bufW, int bufH, bool skipGpuImagePanels,
                               const BYTE* slideshowFrame = nullptr, int ssFrameW = 0, int ssFrameH = 0);

public:

#if defined(VDJ_WIN)
    // GDI+ for panel rendering (public for static helper access)
    static ULONG_PTR      s_gdiToken;
    static int            s_gdiRef;
#endif

private:
    VDJ_WINDOW  m_settingsWnd;

    // Rendering / media engine
    TmRenderer            m_renderer;
    TmMedia               m_media;
#if defined(VDJ_WIN)
    ID3D11Device*         m_d3dDevice;
    ID3D11DeviceContext*  m_d3dContext;
#elif defined(VDJ_MAC)
    CGLContextObj         m_glContext;
#endif
    bool                  m_d3dReady;
    wchar_t               m_currentPath[MAX_PATH];
    wchar_t               m_songTitle[256];
    wchar_t               m_songArtist[256];
    wchar_t               m_songDisplay[512];

    // Overlay panels for layout tab
    OverlayPanel          m_overlayPanels[MAX_OVERLAY_PANELS];
    int                   m_numOverlayPanels;

    // Pre-loaded overlay images, one per panel index.
#if defined(VDJ_WIN)
    Gdiplus::Image*       m_overlayImages[MAX_OVERLAY_PANELS];
    ID3D11ShaderResourceView* m_overlayImageSRVs[MAX_OVERLAY_PANELS];
#elif defined(VDJ_MAC)
    CGImageRef            m_overlayImages[MAX_OVERLAY_PANELS];
    GLuint                m_overlayImageTexs[MAX_OVERLAY_PANELS];
#endif
    std::vector<BYTE>     m_overlayImagePixels[MAX_OVERLAY_PANELS];
    int                   m_overlayImagePixelW[MAX_OVERLAY_PANELS];
    int                   m_overlayImagePixelH[MAX_OVERLAY_PANELS];
#if defined(VDJ_WIN)
    CRITICAL_SECTION      m_overlayLock;
#elif defined(VDJ_MAC)
    void*                 m_overlayLock;  // pthread_mutex_t*
#endif
    bool                  m_overlayLockInit;

    // Declared VDJ parameters
    int   m_nOpenFile;
    int   m_nPlay;
    int   m_nStop;
    int   m_nPause;
    float m_fVolume;
    float m_fAlpha;
    int   m_bLooping;

    // Phase 1 playback intent (consumed by Phase 2 renderer)
    bool  m_playing;
    int   m_scaleMode;
    int   m_transitionMode;  // 0=Cut 1=Fade 2=Crossfade
    float m_transitionAlpha;  // Current alpha for fade transitions
    int   m_curBank;
    int   m_curSlot;
    int   m_renderModeRequested;
    int   m_renderModeEffective;
    float m_renderAvgMs;

    // Slideshow frame cache for CPU overlay blit path (same path as image panels)
    std::vector<BYTE>     m_slideshowFrameCache;
    int                   m_slideshowFrameW;
    int                   m_slideshowFrameH;
    bool                  m_slideshowFrameValid;

    // Previous frame cache for crossfade/fade transitions
    std::vector<BYTE>     m_slideshowPrevCache;   // scaled to same dims as current frame
    int                   m_slideshowPrevW;
    int                   m_slideshowPrevH;
    bool                  m_slideshowPrevValid;
    float                 m_transitionProgress;   // 0.0 = prev, 1.0 = current (advances per frame)

    // Audio analysis for music-reactive shaders
    struct AudioAnalysis {
        float level;      // overall loudness 0..1 (RMS)
        float bass;       // bass band energy 0..1
        float mid;        // mid band energy 0..1
        float treble;     // treble band energy 0..1
        float beat;       // beat pulse 0..1
        float bassState[4]; // IIR filter state for bass
        float midState[4];  // IIR filter state for mid
        float trebleState[4]; // IIR filter state for treble
        int   sampleRate;
    };
    AudioAnalysis         m_audio;
#if defined(VDJ_WIN)
    CRITICAL_SECTION      m_audioLock;
#elif defined(VDJ_MAC)
    void*                 m_audioLock;  // pthread_mutex_t*
#endif
    bool                  m_audioLockInit;

    // Shader settings
    bool                  m_shadersEnabled;
    bool                  m_shaderDisableOnKaraoke;
    bool                  m_karaokeAutoHide;
    int                   m_mainShaderIndex;
    bool                  m_isKaraoke;      // cached karaoke state
    bool                  m_isAudioOnly;    // cached audio-only state
    int                   m_karaokeCheckFrame; // frame counter for throttled checks
    int                   m_audioOnlyCheckFrame;

    LicenseState          m_license;
};
