#pragma once
#include "vdj/vdjVideo8.h"
#include "tm/TmTypes.h"
#include <vector>

#if defined(VDJ_WIN)
#include <d3d11.h>
#elif defined(VDJ_MAC)
#include <OpenGL/gl.h>
#include <OpenGL/CGLCurrent.h>
#endif

// Owns the GPU resources used to upload a BGRA frame buffer and draw it
// (with alpha) onto the VirtualDJ deck texture.
// Windows: Direct3D 11 (device/context from VDJ)
// macOS: OpenGL (CGL context from VDJ)
class TmRenderer {
public:
    TmRenderer();
    ~TmRenderer();

#if defined(VDJ_WIN)
    // Bind to the VDJ-provided D3D11 device.
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
    ID3D11DeviceContext* Context() const { return m_context; }
#elif defined(VDJ_MAC)
    // Bind to the VDJ-provided OpenGL context.
    bool Init(CGLContextObj ctx);
    CGLContextObj Context() const { return m_glContext; }
#endif
    void Shutdown();
    bool Ready() const { return m_ready; }

    // (Re)create the frame texture to a given size. Safe to call repeatedly.
    bool EnsureFrameTexture(int w, int h);

    // Upload a top-down BGRA buffer (stride bytes per row) into the frame texture.
    void UploadFrame(const void* bgra, int w, int h, int stride);

    // Save current frame as previous frame (call before starting new media for crossfade).
    void SaveCurrentAsPrevious();

    // Draw the current frame texture as a full-screen quad with the given alpha.
    void DrawFullscreen(float alpha);

    // Draw with crossfade: blend current frame with previous frame
    void DrawCrossfade(float alpha, float crossfadeProgress);

    // ─── Overlay panel rendering ─────────────────────────────────────────────────
    void UploadOverlay(const void* bgra, int w, int h, int stride);
    void DrawOverlay();

#if defined(VDJ_WIN)
    ID3D11ShaderResourceView* GetOverlaySRV() const { return m_overlaySRV; }
#elif defined(VDJ_MAC)
    GLuint GetOverlayTexture() const { return m_overlayTex; }
#endif

    // Draw current frame texture within a specific rectangle (normalized 0-1)
    void DrawRect(float x, float y, float w, float h, float alpha);

#if defined(VDJ_WIN)
    void DrawRectWithTexture(float x, float y, float w, float h, float alpha, ID3D11ShaderResourceView* srv,
                             float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f);
    void DrawFullscreenWithTexture(float alpha, ID3D11ShaderResourceView* srv);
#elif defined(VDJ_MAC)
    void DrawRectWithTexture(float x, float y, float w, float h, float alpha, GLuint tex,
                             float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f);
    void DrawFullscreenWithTexture(float alpha, GLuint tex);
#endif

    void ClearToBlack();

    // ─── Custom shader rendering ───────────────────────────────────────────────
    struct ShaderUniforms {
        float iResolution[2];
        float iTime;
        float iBeat;
        float iLevel;
        float iBass;
        float iMid;
        float iTreble;
        float iBpm;
        float iSongPosBeats;
        float _pad;
    };

    bool LoadShaders(const wchar_t* dllDir);
    void ReloadShaders(const wchar_t* dllDir);

    int GetShaderCount() const;
    const wchar_t* GetShaderName(int index) const;

    void DrawShaderInRect(int shaderIndex, float x, float y, float w, float h, const ShaderUniforms& uniforms);

    bool RenderShaderToPixels(int shaderIndex, int pixelW, int pixelH,
                              const ShaderUniforms& uniforms, std::vector<BYTE>& outPixels);

private:
    void ReleaseAll();
    void SetFullscreenQuad();

#if defined(VDJ_WIN)
    // ── Windows: D3D11 implementation ──
    ID3D11Device*             m_device;
    ID3D11DeviceContext*      m_context;
    ID3D11Texture2D*          m_frameTex;
    ID3D11ShaderResourceView* m_frameSRV;
    ID3D11Texture2D*          m_prevFrameTex;
    ID3D11ShaderResourceView* m_prevFrameSRV;
    ID3D11Texture2D*          m_overlayTex;
    ID3D11ShaderResourceView* m_overlaySRV;
    ID3D11VertexShader*       m_vs;
    ID3D11PixelShader*        m_ps;
    ID3D11PixelShader*        m_psCrossfade;
    ID3D11InputLayout*        m_layout;
    ID3D11Buffer*             m_vb;
    ID3D11Buffer*             m_alphaCB;
    ID3D11Buffer*             m_crossfadeCB;
    ID3D11SamplerState*       m_sampler;
    ID3D11BlendState*         m_blend;
    ID3D11DepthStencilState*  m_depthState;
    int                       m_texW, m_texH;
    int                       m_prevTexW, m_prevTexH;
    bool                      m_hasPrevFrame;
    std::vector<BYTE>         m_currFrameData;

    static const int MAX_SHADERS = 200;
    struct ShaderInfo {
        wchar_t name[64];
        ID3D11PixelShader* ps;
    };
    ShaderInfo               m_shaders[MAX_SHADERS];
    int                       m_shaderCount;
    ID3D11Buffer*             m_shaderCB;
    ID3D11VertexShader*       m_shaderVS;
    ID3D11Texture2D*          m_shaderRTTex;
    ID3D11RenderTargetView*   m_shaderRTV;
    ID3D11Texture2D*          m_shaderReadbackTex;
    int                       m_shaderRTW;
    int                       m_shaderRTH;

#elif defined(VDJ_MAC)
    // ── macOS: OpenGL implementation ──
    CGLContextObj             m_glContext;
    GLuint                    m_frameTex;
    GLuint                    m_prevFrameTex;
    GLuint                    m_overlayTex;
    GLuint                    m_vao;
    GLuint                    m_vb;
    GLuint                    m_shaderProg;
    GLuint                    m_crossfadeProg;
    GLuint                    m_sampler;
    int                       m_texW, m_texH;
    int                       m_prevTexW, m_prevTexH;
    bool                      m_hasPrevFrame;
    std::vector<BYTE>         m_currFrameData;

    static const int MAX_SHADERS = 200;
    struct ShaderInfo {
        wchar_t name[64];
        GLuint   prog;
    };
    ShaderInfo               m_shaders[MAX_SHADERS];
    int                       m_shaderCount;
    GLuint                    m_shaderUBO;
    GLuint                    m_shaderFBO;
    GLuint                    m_shaderRTTex;
    int                       m_shaderRTW;
    int                       m_shaderRTH;
#endif

    bool                      m_ready;
};
