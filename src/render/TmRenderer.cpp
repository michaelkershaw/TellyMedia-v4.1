#include "tm/TmRenderer.h"
#include "tm/TmLogger.h"
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <windows.h>
// Embedded precompiled shader bytecode (generated at build time)
#include "EmbeddedShaders.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Simple textured-quad shaders. Vertex format matches VDJ's TVertex.
static const char* kHLSL = R"(
cbuffer AlphaCB : register(b0) { float gAlpha; float gUseTexAlpha; float2 _pad; };
Texture2D    tex0 : register(t0);
SamplerState smp0 : register(s0);
struct VSIn  { float3 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 1.0);
    o.uv  = i.uv;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    float4 c = tex0.Sample(smp0, i.uv);
    // gUseTexAlpha=1: respect texture alpha (overlay panels).
    // gUseTexAlpha=0: force opaque alpha (deck/media - VDJ render targets often have alpha=0).
    c.a = lerp(gAlpha, c.a * gAlpha, gUseTexAlpha);
    return c;
}
)";

// Crossfade shader: blends current frame (tex0) with previous frame (tex1)
static const char* kCrossfadeHLSL = R"(
cbuffer CrossfadeCB : register(b0) { float gAlpha; float gMix; float2 _pad; };
Texture2D    tex0 : register(t0);
Texture2D    tex1 : register(t1);
SamplerState smp0 : register(s0);
struct VSIn  { float3 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 1.0);
    o.uv  = i.uv;
    return o;
}
float4 PSCrossfade(VSOut i) : SV_TARGET {
    float4 c0 = tex0.Sample(smp0, i.uv);
    float4 c1 = tex1.Sample(smp0, i.uv);
    float4 c = lerp(c1, c0, gMix);
    c.a *= gAlpha;
    return c;
}
)";

// Vertex shader for custom shaders (uses SV_VertexID to generate fullscreen quad)
static const char* kShaderVS = R"(
cbuffer ShaderCB : register(b0) {
    float2 iResolution;
    float  iTime;
    float  iBeat;
    float  iLevel;
    float  iBass, iMid, iTreble;
    float  iBpm;
    float  iSongPosBeats;
    float  _pad;
    float panelX, panelY, panelW, panelH;
};

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut main(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id & 2) >> 1, id & 1);
    
    // Scale to panel bounds instead of fullscreen
    float2 pos = float2(panelX + uv.x * panelW, panelY + uv.y * panelH) * 2.0f - 1.0f;
    pos.y = -pos.y; // Flip Y for DirectX
    
    o.pos = float4(pos, 0.0f, 1.0f);
    o.uv = uv;
    return o;
}
)";

static std::string NormalizeLegacyShaderSource(const char* src)
{
    if (!src) return {};

    std::string code(src);
    if (code.find("cbuffer ShaderParams") == std::string::npos) {
        return code;
    }

    const size_t blockStart = code.find("cbuffer ShaderParams");
    const size_t openBrace = code.find('{', blockStart);
    const size_t closeBrace = code.find("};", openBrace);
    if (openBrace == std::string::npos || closeBrace == std::string::npos || closeBrace <= openBrace) {
        return code;
    }

    std::string out;
    out.reserve(code.size() + 256);
    out.append(code.begin(), code.begin() + blockStart);
    out += R"(
cbuffer ShaderCB : register(b0) {
    float2 iResolution;
    float  iTime;
    float  iBeat;
    float  iLevel;
    float  iBass;
    float  iMid;
    float  iTreble;
    float  iBpm;
    float  iSongPosBeats;
    float  _pad;
};

#define time iTime
#define resX iResolution.x
#define resY iResolution.y
#define bass iBass
#define mid iMid
#define treble iTreble
#define bpm iBpm
#define beat iBeat
)";
    out.append(code.begin() + closeBrace + 2, code.end());
    return out;
}

struct QuadVertex { float x, y, z; DWORD color; float u, v; };

TmRenderer::TmRenderer()
    : m_device(nullptr), m_context(nullptr), m_frameTex(nullptr), m_frameSRV(nullptr),
      m_prevFrameTex(nullptr), m_prevFrameSRV(nullptr), m_overlayTex(nullptr), m_overlaySRV(nullptr),
      m_vs(nullptr), m_ps(nullptr), m_psCrossfade(nullptr), m_layout(nullptr), m_vb(nullptr), m_alphaCB(nullptr), m_crossfadeCB(nullptr),
      m_sampler(nullptr), m_blend(nullptr), m_texW(0), m_texH(0), m_prevTexW(0), m_prevTexH(0),
      m_ready(false), m_hasPrevFrame(false), m_shaderCount(0), m_shaderCB(nullptr), m_shaderVS(nullptr),
      m_shaderRTTex(nullptr), m_shaderRTV(nullptr), m_shaderReadbackTex(nullptr), m_shaderRTW(0), m_shaderRTH(0)
{
    ZeroMemory(m_shaders, sizeof(m_shaders));
}

TmRenderer::~TmRenderer() { Shutdown(); }

bool TmRenderer::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context) return false;
    m_device = device;
    m_context = context;

    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(kHLSL, strlen(kHLSL), nullptr, nullptr, nullptr,
                            "VSMain", "vs_4_0", 0, 0, &vsb, &err);
    if (FAILED(hr)) { TM_HR("VS compile", hr); if (err) err->Release(); return false; }
    if (err) { err->Release(); err = nullptr; }

    hr = D3DCompile(kHLSL, strlen(kHLSL), nullptr, nullptr, nullptr,
                    "PSMain", "ps_4_0", 0, 0, &psb, &err);
    if (FAILED(hr)) { TM_HR("PS compile", hr); if (err) err->Release(); if (vsb) vsb->Release(); return false; }
    if (err) { err->Release(); err = nullptr; }

    hr = m_device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs);
    if (FAILED(hr)) { TM_HR("CreateVS", hr); vsb->Release(); psb->Release(); return false; }
    hr = m_device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps);
    if (FAILED(hr)) { TM_HR("CreatePS", hr); vsb->Release(); psb->Release(); return false; }
    psb->Release();

    // Compile crossfade pixel shader
    ID3DBlob* psbCross = nullptr;
    hr = D3DCompile(kCrossfadeHLSL, strlen(kCrossfadeHLSL), nullptr, nullptr, nullptr,
                    "PSCrossfade", "ps_4_0", 0, 0, &psbCross, &err);
    if (FAILED(hr)) { TM_HR("Crossfade PS compile", hr); if (err) err->Release(); vsb->Release(); return false; }
    if (err) { err->Release(); err = nullptr; }

    hr = m_device->CreatePixelShader(psbCross->GetBufferPointer(), psbCross->GetBufferSize(), nullptr, &m_psCrossfade);
    if (FAILED(hr)) { TM_HR("CreateCrossfadePS", hr); vsb->Release(); psbCross->Release(); return false; }
    psbCross->Release();

    // Create vertex shader for custom shaders (uses SV_VertexID)
    ID3DBlob* shaderVSBlob = nullptr;
    ID3DBlob* shaderVSErr = nullptr;
    hr = D3DCompile(kShaderVS, strlen(kShaderVS), nullptr, nullptr, nullptr,
                    "main", "vs_4_0", 0, 0, &shaderVSBlob, &shaderVSErr);
    if (FAILED(hr)) {
        if (shaderVSErr) {
            TM_WARN("Shader VS compile failed: %s", (char*)shaderVSErr->GetBufferPointer());
            shaderVSErr->Release();
        }
        vsb->Release();
        return false;
    }
    if (shaderVSErr) shaderVSErr->Release();

    hr = m_device->CreateVertexShader(shaderVSBlob->GetBufferPointer(), shaderVSBlob->GetBufferSize(), nullptr, &m_shaderVS);
    shaderVSBlob->Release();
    if (FAILED(hr)) { TM_HR("CreateShaderVS", hr); vsb->Release(); return false; }

    D3D11_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = m_device->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_layout);
    vsb->Release();
    if (FAILED(hr)) { TM_HR("CreateInputLayout", hr); return false; }

    // Fullscreen quad (two triangles), NDC coords, V flipped for top-down BGRA.
    QuadVertex verts[6] = {
        { -1.f,  1.f, 0.f, 0xFFFFFFFF, 0.f, 0.f },
        {  1.f,  1.f, 0.f, 0xFFFFFFFF, 1.f, 0.f },
        { -1.f, -1.f, 0.f, 0xFFFFFFFF, 0.f, 1.f },
        {  1.f,  1.f, 0.f, 0xFFFFFFFF, 1.f, 0.f },
        {  1.f, -1.f, 0.f, 0xFFFFFFFF, 1.f, 1.f },
        { -1.f, -1.f, 0.f, 0xFFFFFFFF, 0.f, 1.f },
    };
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(verts);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = verts;
    hr = m_device->CreateBuffer(&bd, &sd, &m_vb);
    if (FAILED(hr)) { TM_HR("CreateVB", hr); return false; }

    D3D11_BUFFER_DESC cb = {};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.ByteWidth = 16;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_device->CreateBuffer(&cb, nullptr, &m_alphaCB);
    if (FAILED(hr)) { TM_HR("CreateAlphaCB", hr); return false; }

    // Crossfade constant buffer (alpha + mix)
    cb.ByteWidth = 16;
    hr = m_device->CreateBuffer(&cb, nullptr, &m_crossfadeCB);
    if (FAILED(hr)) { TM_HR("CreateCrossfadeCB", hr); return false; }

    // Shader constant buffer (ShaderUniforms: 11 floats + 4 panel floats = 60 bytes, round to 16-byte boundary = 64)
    cb.ByteWidth = 64;
    hr = m_device->CreateBuffer(&cb, nullptr, &m_shaderCB);
    if (FAILED(hr)) { TM_HR("CreateShaderCB", hr); return false; }

    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_device->CreateSamplerState(&sm, &m_sampler);
    if (FAILED(hr)) { TM_HR("CreateSampler", hr); return false; }

    D3D11_BLEND_DESC bl = {};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_device->CreateBlendState(&bl, &m_blend);
    if (FAILED(hr)) { TM_HR("CreateBlend", hr); return false; }

    // Depth stencil state: disable depth test so shader draws on top of deck
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.StencilEnable = FALSE;
    hr = m_device->CreateDepthStencilState(&ds, &m_depthState);
    if (FAILED(hr)) { TM_HR("CreateDepthStencilState", hr); return false; }

    // Create overlay texture (1920x1080 for panels)
    D3D11_TEXTURE2D_DESC otd = {};
    otd.Width = 1920; otd.Height = 1080;
    otd.MipLevels = 1; otd.ArraySize = 1;
    otd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    otd.SampleDesc.Count = 1;
    otd.Usage = D3D11_USAGE_DYNAMIC;
    otd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    otd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_device->CreateTexture2D(&otd, nullptr, &m_overlayTex);
    if (FAILED(hr)) { TM_HR("CreateOverlayTex", hr); return false; }
    hr = m_device->CreateShaderResourceView(m_overlayTex, nullptr, &m_overlaySRV);
    if (FAILED(hr)) { TM_HR("CreateOverlaySRV", hr); return false; }

    m_ready = true;
    TM_INFO("TmRenderer initialized");
    return true;
}

bool TmRenderer::EnsureFrameTexture(int w, int h)
{
    if (!m_device || w <= 0 || h <= 0) return false;
    if (m_frameTex && m_texW == w && m_texH == h) return true;

    // Only manage the CURRENT frame texture here. The previous texture has its
    // own independent dimensions and must NOT be destroyed on a current resize,
    // otherwise crossfades between differently-sized images lose the prev frame.
    if (m_frameSRV) { m_frameSRV->Release(); m_frameSRV = nullptr; }
    if (m_frameTex) { m_frameTex->Release(); m_frameTex = nullptr; }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &m_frameTex);
    if (FAILED(hr)) { TM_HR("CreateFrameTex", hr); return false; }

    hr = m_device->CreateShaderResourceView(m_frameTex, nullptr, &m_frameSRV);
    if (FAILED(hr)) { TM_HR("CreateFrameSRV", hr); return false; }

    m_texW = w; m_texH = h;
    return true;
}

void TmRenderer::SaveCurrentAsPrevious()
{
    // Snapshot the current frame into the PREVIOUS texture at the CURRENT
    // dimensions. The previous texture is (re)created here at its own size so
    // it survives any later resize of the current texture (different image
    // dimensions between slots).
    if (!m_device || !m_context || m_currFrameData.empty() || m_texW == 0 || m_texH == 0) {
        TM_WARN("SaveCurrentAsPrevious: nothing to save currSize=%zu texW=%d texH=%d",
                m_currFrameData.size(), m_texW, m_texH);
        return;
    }

    int rowBytes = m_texW * 4;
    if (m_currFrameData.size() != (size_t)rowBytes * m_texH) {
        TM_WARN("SaveCurrentAsPrevious: curr size mismatch expected=%d actual=%zu",
                rowBytes * m_texH, m_currFrameData.size());
        return;
    }

    // (Re)create previous texture if dimensions changed
    if (!m_prevFrameTex || m_prevTexW != m_texW || m_prevTexH != m_texH) {
        if (m_prevFrameSRV) { m_prevFrameSRV->Release(); m_prevFrameSRV = nullptr; }
        if (m_prevFrameTex) { m_prevFrameTex->Release(); m_prevFrameTex = nullptr; }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = m_texW; td.Height = m_texH;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &m_prevFrameTex);
        if (FAILED(hr)) { TM_HR("SaveCurrentAsPrevious CreatePrevTex", hr); return; }
        hr = m_device->CreateShaderResourceView(m_prevFrameTex, nullptr, &m_prevFrameSRV);
        if (FAILED(hr)) { TM_HR("SaveCurrentAsPrevious CreatePrevSRV", hr); return; }
        m_prevTexW = m_texW; m_prevTexH = m_texH;
    }

    // Upload current CPU data into previous texture
    D3D11_MAPPED_SUBRESOURCE prevMap = {};
    if (SUCCEEDED(m_context->Map(m_prevFrameTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &prevMap))) {
        for (int y = 0; y < m_prevTexH; ++y)
            memcpy((BYTE*)prevMap.pData + y * prevMap.RowPitch, &m_currFrameData[y * rowBytes], rowBytes);
        m_context->Unmap(m_prevFrameTex, 0);
        m_hasPrevFrame = true;
        TM_INFO("SaveCurrentAsPrevious: saved prev %dx%d", m_prevTexW, m_prevTexH);
    } else {
        TM_WARN("SaveCurrentAsPrevious: failed to map prev texture");
    }
}

void TmRenderer::UploadFrame(const void* bgra, int w, int h, int stride)
{
    if (!m_context || !bgra) return;
    if (!EnsureFrameTexture(w, h)) return;

    int rowBytes = w * 4;

    // Save incoming frame to current CPU buffer (used later by SaveCurrentAsPrevious)
    if (m_currFrameData.size() != (size_t)rowBytes * h) {
        m_currFrameData.resize((size_t)rowBytes * h);
    }
    const BYTE* src = (const BYTE*)bgra;
    for (int y = 0; y < h; ++y)
        memcpy(&m_currFrameData[y * rowBytes], src + y * stride, rowBytes);

    // Upload new frame to current texture
    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(m_context->Map(m_frameTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    BYTE* dst = (BYTE*)map.pData;
    for (int y = 0; y < h; ++y)
        memcpy(dst + y * map.RowPitch, src + y * stride, rowBytes);
    m_context->Unmap(m_frameTex, 0);
}

void TmRenderer::SetFullscreenQuad()
{
    if (!m_context || !m_vb) return;
    QuadVertex verts[6] = {
        { -1.f,  1.f, 0.f, 0xFFFFFFFF, 0.f, 0.f },
        {  1.f,  1.f, 0.f, 0xFFFFFFFF, 1.f, 0.f },
        { -1.f, -1.f, 0.f, 0xFFFFFFFF, 0.f, 1.f },
        {  1.f,  1.f, 0.f, 0xFFFFFFFF, 1.f, 0.f },
        {  1.f, -1.f, 0.f, 0xFFFFFFFF, 1.f, 1.f },
        { -1.f, -1.f, 0.f, 0xFFFFFFFF, 0.f, 1.f },
    };
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, verts, sizeof(verts));
        m_context->Unmap(m_vb, 0);
    }
}

void TmRenderer::DrawFullscreen(float alpha)
{
    if (!m_ready || !m_frameSRV) return;
    SetFullscreenQuad();

    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_alphaCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[4] = { alpha, 0, 0, 0 };
        memcpy(m.pData, data, sizeof(data));
        m_context->Unmap(m_alphaCB, 0);
    }

    UINT stride = sizeof(QuadVertex), offset = 0;
    m_context->IASetInputLayout(m_layout);
    m_context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_alphaCB);
    m_context->PSSetShaderResources(0, 1, &m_frameSRV);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->Draw(6, 0);
}

void TmRenderer::DrawCrossfade(float alpha, float crossfadeProgress)
{
    if (!m_ready || !m_frameSRV || !m_prevFrameSRV) {
        // No previous frame yet: fall back to a normal draw so we still show the image
        DrawFullscreen(alpha);
        return;
    }

    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_crossfadeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[4] = { alpha, crossfadeProgress, 0, 0 };
        memcpy(m.pData, data, sizeof(data));
        m_context->Unmap(m_crossfadeCB, 0);
    }

    UINT stride = sizeof(QuadVertex), offset = 0;
    m_context->IASetInputLayout(m_layout);
    m_context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_psCrossfade, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_crossfadeCB);
    ID3D11ShaderResourceView* srvs[2] = { m_frameSRV, m_prevFrameSRV };
    m_context->PSSetShaderResources(0, 2, srvs);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->Draw(6, 0);
}

void TmRenderer::ReleaseAll()
{
    if (m_frameSRV) { m_frameSRV->Release(); m_frameSRV = nullptr; }
    if (m_frameTex) { m_frameTex->Release(); m_frameTex = nullptr; }
    if (m_prevFrameSRV) { m_prevFrameSRV->Release(); m_prevFrameSRV = nullptr; }
    if (m_prevFrameTex) { m_prevFrameTex->Release(); m_prevFrameTex = nullptr; }
    if (m_overlaySRV) { m_overlaySRV->Release(); m_overlaySRV = nullptr; }
    if (m_overlayTex) { m_overlayTex->Release(); m_overlayTex = nullptr; }
    if (m_vs)       { m_vs->Release();       m_vs = nullptr; }
    if (m_ps)       { m_ps->Release();       m_ps = nullptr; }
    if (m_psCrossfade) { m_psCrossfade->Release(); m_psCrossfade = nullptr; }
    if (m_layout)   { m_layout->Release();   m_layout = nullptr; }
    if (m_vb)       { m_vb->Release();       m_vb = nullptr; }
    if (m_alphaCB)  { m_alphaCB->Release();  m_alphaCB = nullptr; }
    if (m_crossfadeCB) { m_crossfadeCB->Release(); m_crossfadeCB = nullptr; }
    if (m_sampler)  { m_sampler->Release();  m_sampler = nullptr; }
    if (m_blend)    { m_blend->Release();    m_blend = nullptr; }
    if (m_depthState) { m_depthState->Release(); m_depthState = nullptr; }

    // Release shader resources
    for (int i = 0; i < m_shaderCount; ++i) {
        if (m_shaders[i].ps) {
            m_shaders[i].ps->Release();
            m_shaders[i].ps = nullptr;
        }
    }
    m_shaderCount = 0;
    ZeroMemory(m_shaders, sizeof(m_shaders));
    if (m_shaderCB) { m_shaderCB->Release(); m_shaderCB = nullptr; }
    if (m_shaderVS) { m_shaderVS->Release(); m_shaderVS = nullptr; }
    if (m_shaderRTV) { m_shaderRTV->Release(); m_shaderRTV = nullptr; }
    if (m_shaderRTTex) { m_shaderRTTex->Release(); m_shaderRTTex = nullptr; }
    if (m_shaderReadbackTex) { m_shaderReadbackTex->Release(); m_shaderReadbackTex = nullptr; }

    m_texW = m_texH = 0;
    m_prevTexW = m_prevTexH = 0;
    m_shaderRTW = m_shaderRTH = 0;
    m_hasPrevFrame = false;
}

void TmRenderer::UploadOverlay(const void* bgra, int w, int h, int stride)
{
    if (!m_context || !bgra || !m_overlayTex) return;
    
    int rowBytes = w * 4;
    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(m_context->Map(m_overlayTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    
    const BYTE* src = (const BYTE*)bgra;
    BYTE* dst = (BYTE*)map.pData;
    for (int y = 0; y < h; ++y)
        memcpy(dst + y * map.RowPitch, src + y * stride, rowBytes);
    m_context->Unmap(m_overlayTex, 0);
}

void TmRenderer::DrawOverlay()
{
    if (!m_ready || !m_overlaySRV) return;
    SetFullscreenQuad();

    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_alphaCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[4] = { 1.0f, 1.0f, 0, 0 }; // Full alpha, respect texture alpha for overlay transparency
        memcpy(m.pData, data, sizeof(data));
        m_context->Unmap(m_alphaCB, 0);
    }

    UINT stride = sizeof(QuadVertex), offset = 0;
    m_context->IASetInputLayout(m_layout);
    m_context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_alphaCB);
    m_context->PSSetShaderResources(0, 1, &m_overlaySRV);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->Draw(6, 0);
}

void TmRenderer::DrawRect(float x, float y, float w, float h, float alpha)
{
    if (!m_ready || !m_frameSRV) return;

    // Convert normalized coords to NDC clip space
    float x1 = x * 2.0f - 1.0f;
    float y1 = 1.0f - y * 2.0f;
    float x2 = (x + w) * 2.0f - 1.0f;
    float y2 = 1.0f - (y + h) * 2.0f;

    QuadVertex verts[6] = {
        { x1,  y1, 0.f, 0xFFFFFFFF, 0.f, 0.f },
        { x2,  y1, 0.f, 0xFFFFFFFF, 1.f, 0.f },
        { x1,  y2, 0.f, 0xFFFFFFFF, 0.f, 1.f },
        { x2,  y1, 0.f, 0xFFFFFFFF, 1.f, 0.f },
        { x2,  y2, 0.f, 0xFFFFFFFF, 1.f, 1.f },
        { x1,  y2, 0.f, 0xFFFFFFFF, 0.f, 1.f },
    };

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, verts, sizeof(verts));
        m_context->Unmap(m_vb, 0);
    }

    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_alphaCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[4] = { alpha, 0, 0, 0 };
        memcpy(m.pData, data, sizeof(data));
        m_context->Unmap(m_alphaCB, 0);
    }

    UINT stride = sizeof(QuadVertex), offset = 0;
    m_context->IASetInputLayout(m_layout);
    m_context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_alphaCB);
    m_context->PSSetShaderResources(0, 1, &m_frameSRV);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->Draw(6, 0);
}

void TmRenderer::DrawRectWithTexture(float x, float y, float w, float h, float alpha, ID3D11ShaderResourceView* srv,
                                     float u0, float v0, float u1, float v1)
{
    if (!m_ready || !srv) return;

    // Convert normalized coords to NDC clip space
    float x1 = x * 2.0f - 1.0f;
    float y1 = 1.0f - y * 2.0f;
    float x2 = (x + w) * 2.0f - 1.0f;
    float y2 = 1.0f - (y + h) * 2.0f;

    QuadVertex verts[6] = {
        { x1,  y1, 0.f, 0xFFFFFFFF, u0, v0 },
        { x2,  y1, 0.f, 0xFFFFFFFF, u1, v0 },
        { x1,  y2, 0.f, 0xFFFFFFFF, u0, v1 },
        { x2,  y1, 0.f, 0xFFFFFFFF, u1, v0 },
        { x2,  y2, 0.f, 0xFFFFFFFF, u1, v1 },
        { x1,  y2, 0.f, 0xFFFFFFFF, u0, v1 },
    };

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, verts, sizeof(verts));
        m_context->Unmap(m_vb, 0);
    }

    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_alphaCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[4] = { alpha, 1.0f, 0, 0 };
        memcpy(m.pData, data, sizeof(data));
        m_context->Unmap(m_alphaCB, 0);
    }

    UINT stride = sizeof(QuadVertex), offset = 0;
    m_context->IASetInputLayout(m_layout);
    m_context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_alphaCB);
    m_context->PSSetShaderResources(0, 1, &srv);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->Draw(6, 0);
}

void TmRenderer::DrawFullscreenWithTexture(float alpha, ID3D11ShaderResourceView* srv)
{
    if (!m_ready || !srv) return;
    SetFullscreenQuad();

    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_alphaCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[4] = { alpha, 0, 0, 0 };
        memcpy(m.pData, data, sizeof(data));
        m_context->Unmap(m_alphaCB, 0);
    }

    UINT stride = sizeof(QuadVertex), offset = 0;
    m_context->IASetInputLayout(m_layout);
    m_context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_alphaCB);
    m_context->PSSetShaderResources(0, 1, &srv);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->Draw(6, 0);
}

void TmRenderer::ClearToBlack()
{
    if (!m_ready || !m_context) return;

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    m_context->OMGetRenderTargets(1, &rtv, &dsv);
    if (rtv) {
        float clear[4] = { 0, 0, 0, 1 };
        m_context->ClearRenderTargetView(rtv, clear);
        rtv->Release();
    }
    if (dsv) dsv->Release();
}

// ─── Custom shader rendering ───────────────────────────────────────────────────

// Helper to extract compiled_D11_1.cso from a .vdjshader ZIP file in memory
static bool ExtractCsoFromVdjShader(const wchar_t* vdjshaderPath, std::vector<BYTE>& outBytes)
{
    outBytes.clear();
    
    // Read the entire ZIP file into memory
    HANDLE hFile = CreateFileW(vdjshaderPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0 || fileSize > 16 * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }
    
    std::vector<BYTE> zipData(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, zipData.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    
    // Simple ZIP parser - find compiled_D11_1.cso entry
    const char* targetName = "compiled_D11_1.cso";
    const size_t targetNameLen = strlen(targetName);
    
    size_t pos = 0;
    while (pos < zipData.size() - 30) {
        // Check for local file header signature (0x04034b50)
        if (zipData[pos] == 0x50 && zipData[pos + 1] == 0x4b && zipData[pos + 2] == 0x03 && zipData[pos + 3] == 0x04) {
            // Parse local file header
            uint16_t nameLen = *(uint16_t*)&zipData[pos + 26];
            uint16_t extraLen = *(uint16_t*)&zipData[pos + 28];
            uint32_t compressedSize = *(uint32_t*)&zipData[pos + 18];
            uint16_t compressionMethod = *(uint16_t*)&zipData[pos + 8];
            
            // Check filename
            const char* filename = (const char*)&zipData[pos + 30];
            if (nameLen == targetNameLen && memcmp(filename, targetName, targetNameLen) == 0) {
                // Found the target file
                size_t dataOffset = pos + 30 + nameLen + extraLen;
                
                // For stored (uncompressed) files, just copy the data
                if (compressionMethod == 0) {
                    if (dataOffset + compressedSize <= zipData.size()) {
                        outBytes.assign(zipData.begin() + dataOffset, zipData.begin() + dataOffset + compressedSize);
                        return true;
                    }
                }
                // For compressed files, we'd need a deflate decompressor - skip for now
                // Most shader bytecode in .vdjshader is stored uncompressed
                break;
            }
            
            // Skip to next file
            pos += 30 + nameLen + extraLen + compressedSize;
        } else {
            pos++;
        }
    }
    
    return false;
}

bool TmRenderer::LoadShaders(const wchar_t* dllDir)
{
    if (!m_device) return false;

    // Reset current shader list before reloading from disk.
    for (int i = 0; i < m_shaderCount; ++i) {
        if (m_shaders[i].ps) {
            m_shaders[i].ps->Release();
            m_shaders[i].ps = nullptr;
        }
    }
    m_shaderCount = 0;
    ZeroMemory(m_shaders, sizeof(m_shaders));
    // Use fixed VDJ shader path (same as v2)
    wchar_t shaderDir[MAX_PATH];
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\VirtualDJ\\Plugins64\\VideoEffect\\shaders\\", shaderDir, MAX_PATH);

    // Create shaders folder if it doesn't exist
    CreateDirectoryW(shaderDir, nullptr);

    // 1) Load embedded shaders from the DLL (precompiled bytecode)
    const auto& embedded = EmbeddedShaders::Map();
    for (const auto& kv : embedded) {
        if (m_shaderCount >= MAX_SHADERS) break;
        const std::string& name = kv.first;
        const uint8_t* data = kv.second.first;
        size_t size = kv.second.second;
        if (!data || size == 0) continue;
        ID3D11PixelShader* ps = nullptr;
        HRESULT hr = m_device->CreatePixelShader(data, size, nullptr, &ps);
        if (SUCCEEDED(hr) && ps) {
            std::wstring wname(name.begin(), name.end());
            wcsncpy_s(m_shaders[m_shaderCount].name, wname.c_str(), _TRUNCATE);
            m_shaders[m_shaderCount].ps = ps;
            m_shaderCount++;
            TM_INFO("Loaded embedded shader: %S", wname.c_str());
        }
    }

    // 2) Load external precompiled .cso shaders from the folder (instant load)
    wchar_t searchPath[MAX_PATH];
    wcscpy_s(searchPath, shaderDir);
    wcscat_s(searchPath, L"*.cso");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (m_shaderCount >= MAX_SHADERS) break;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            wchar_t filePath[MAX_PATH];
            wcscpy_s(filePath, shaderDir);
            wcscat_s(filePath, fd.cFileName);

            HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) continue;
            DWORD fileSize = GetFileSize(hFile, nullptr);
            if (fileSize == INVALID_FILE_SIZE || fileSize == 0 || fileSize > 4 * 1024 * 1024) {
                CloseHandle(hFile);
                continue;
            }
            std::vector<BYTE> bytes(fileSize);
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(hFile, bytes.data(), fileSize, &bytesRead, nullptr);
            CloseHandle(hFile);
            if (!ok || bytesRead != fileSize) continue;

            ID3D11PixelShader* ps = nullptr;
            HRESULT hr = m_device->CreatePixelShader(bytes.data(), bytes.size(), nullptr, &ps);
            if (SUCCEEDED(hr) && ps) {
                wcsncpy_s(m_shaders[m_shaderCount].name, fd.cFileName, _TRUNCATE);
                m_shaders[m_shaderCount].ps = ps;
                m_shaderCount++;
                TM_INFO("Loaded external CSO shader: %S", fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    // 3) Load .vdjshader files (ZIP archives containing compiled_D11_1.cso)
    wcscpy_s(searchPath, shaderDir);
    wcscat_s(searchPath, L"*.vdjshader");
    hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        TM_INFO("Found .vdjshader files in shader directory");
        do {
            if (m_shaderCount >= MAX_SHADERS) break;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            wchar_t filePath[MAX_PATH];
            wcscpy_s(filePath, shaderDir);
            wcscat_s(filePath, fd.cFileName);

            TM_INFO("Attempting to load VDJ shader: %S", fd.cFileName);

            std::vector<BYTE> csoBytes;
            if (ExtractCsoFromVdjShader(filePath, csoBytes) && !csoBytes.empty()) {
                TM_INFO("Extracted CSO from VDJ shader, size: %zu bytes", csoBytes.size());
                ID3D11PixelShader* ps = nullptr;
                HRESULT hr = m_device->CreatePixelShader(csoBytes.data(), csoBytes.size(), nullptr, &ps);
                if (SUCCEEDED(hr) && ps) {
                    // Use filename without extension as shader name
                    wchar_t name[64];
                    wcscpy_s(name, fd.cFileName);
                    wchar_t* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                    wcsncpy_s(m_shaders[m_shaderCount].name, name, _TRUNCATE);
                    m_shaders[m_shaderCount].ps = ps;
                    m_shaderCount++;
                    TM_INFO("Loaded VDJ shader: %S", name);
                } else {
                    TM_INFO("Failed to create pixel shader from VDJ shader: HRESULT 0x%08X", hr);
                }
            } else {
                TM_INFO("Failed to extract CSO from VDJ shader: %S", fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    } else {
        TM_INFO("No .vdjshader files found in shader directory");
    }

    TM_INFO("Loaded %d shaders (embedded + external .cso + .vdjshader) from %S", m_shaderCount, shaderDir);
    return m_shaderCount > 0;
}

void TmRenderer::ReloadShaders(const wchar_t* dllDir)
{
    // Release existing shaders
    for (int i = 0; i < m_shaderCount; ++i) {
        if (m_shaders[i].ps) {
            m_shaders[i].ps->Release();
            m_shaders[i].ps = nullptr;
        }
    }
    m_shaderCount = 0;
    ZeroMemory(m_shaders, sizeof(m_shaders));

    // Reload
    LoadShaders(dllDir);
}

int TmRenderer::GetShaderCount() const
{
    return m_shaderCount;
}

const wchar_t* TmRenderer::GetShaderName(int index) const
{
    if (index < 0 || index >= m_shaderCount) return L"";
    return m_shaders[index].name;
}

void TmRenderer::DrawShaderInRect(int shaderIndex, float x, float y, float w, float h, const ShaderUniforms& uniforms)
{
    if (!m_ready) { TM_WARN("DrawShaderInRect: m_ready is false"); return; }
    if (!m_context) { TM_WARN("DrawShaderInRect: m_context is null"); return; }
    if (shaderIndex < 0 || shaderIndex >= m_shaderCount) { TM_WARN("DrawShaderInRect: invalid shaderIndex %d (count=%d)", shaderIndex, m_shaderCount); return; }
    if (!m_shaders[shaderIndex].ps) { TM_WARN("DrawShaderInRect: shader %d has null PS", shaderIndex); return; }
    if (!m_shaderVS) { TM_WARN("DrawShaderInRect: m_shaderVS is null"); return; }
    if (!m_shaderCB) { TM_WARN("DrawShaderInRect: m_shaderCB is null"); return; }

    static int shaderDrawLogCount = 0;
    if (++shaderDrawLogCount % 60 == 0) {
        TM_INFO("DrawShaderInRect: drawing shader %d (%S) at (%.2f,%.2f) %.2fx%.2f", shaderIndex, m_shaders[shaderIndex].name, x, y, w, h);
    }

    // Backup DirectX state
    ID3D11VertexShader* oldVS = nullptr;
    ID3D11PixelShader* oldPS = nullptr;
    ID3D11InputLayout* oldLayout = nullptr;
    ID3D11Buffer* oldPSCB[1] = { nullptr };
    ID3D11Buffer* oldVSCB[1] = { nullptr };
    ID3D11SamplerState* oldSampler[1] = { nullptr };
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    m_context->VSGetShader(&oldVS, nullptr, nullptr);
    m_context->PSGetShader(&oldPS, nullptr, nullptr);
    m_context->IAGetInputLayout(&oldLayout);
    m_context->PSGetConstantBuffers(0, 1, oldPSCB);
    m_context->VSGetConstantBuffers(0, 1, oldVSCB);
    m_context->PSGetSamplers(0, 1, oldSampler);
    m_context->IAGetPrimitiveTopology(&oldTopology);

    // Update shader constant buffer with uniforms + panel bounds
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_context->Map(m_shaderCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float* cbData = (float*)m.pData;
        // Copy ShaderUniforms (11 floats)
        memcpy(cbData, &uniforms, sizeof(ShaderUniforms));
        // Add panel bounds (4 floats)
        cbData[11] = x;
        cbData[12] = y;
        cbData[13] = w;
        cbData[14] = h;
        cbData[15] = 0.0f; // padding
        m_context->Unmap(m_shaderCB, 0);
    }

    // Set pipeline state (use SV_VertexID, no vertex buffer needed)
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_context->IASetInputLayout(nullptr);  // VS uses SV_VertexID
    m_context->VSSetShader(m_shaderVS, nullptr, 0);
    m_context->PSSetShader(m_shaders[shaderIndex].ps, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_shaderCB);
    m_context->PSSetConstantBuffers(0, 1, &m_shaderCB);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    float bf[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_context->OMSetDepthStencilState(m_depthState, 0);

    // Draw fullscreen quad (4 vertices as triangle strip)
    m_context->Draw(4, 0);

    // Restore DirectX state
    m_context->VSSetShader(oldVS, nullptr, 0);
    m_context->PSSetShader(oldPS, nullptr, 0);
    m_context->IASetInputLayout(oldLayout);
    m_context->PSSetConstantBuffers(0, 1, oldPSCB);
    m_context->VSSetConstantBuffers(0, 1, oldVSCB);
    m_context->PSSetSamplers(0, 1, oldSampler);
    m_context->IASetPrimitiveTopology(oldTopology);

    // Release backed up state objects
    if (oldVS) oldVS->Release();
    if (oldPS) oldPS->Release();
    if (oldLayout) oldLayout->Release();
    if (oldPSCB[0]) oldPSCB[0]->Release();
    if (oldVSCB[0]) oldVSCB[0]->Release();
    if (oldSampler[0]) oldSampler[0]->Release();
}

bool TmRenderer::RenderShaderToPixels(int shaderIndex, int pixelW, int pixelH,
                                     const ShaderUniforms& uniforms, std::vector<BYTE>& outPixels)
{
    if (!m_ready) return false;
    if (!m_device || !m_context) return false;
    if (shaderIndex < 0 || shaderIndex >= m_shaderCount) return false;
    if (!m_shaders[shaderIndex].ps || !m_shaderVS || !m_shaderCB) return false;
    if (pixelW <= 0 || pixelH <= 0) return false;

    const bool needResize = (!m_shaderRTTex || !m_shaderRTV || !m_shaderReadbackTex ||
                             m_shaderRTW != pixelW || m_shaderRTH != pixelH);
    if (needResize) {
        if (m_shaderRTV) { m_shaderRTV->Release(); m_shaderRTV = nullptr; }
        if (m_shaderRTTex) { m_shaderRTTex->Release(); m_shaderRTTex = nullptr; }
        if (m_shaderReadbackTex) { m_shaderReadbackTex->Release(); m_shaderReadbackTex = nullptr; }

        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = (UINT)pixelW;
        rtDesc.Height = (UINT)pixelH;
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        HRESULT hr = m_device->CreateTexture2D(&rtDesc, nullptr, &m_shaderRTTex);
        if (FAILED(hr) || !m_shaderRTTex) {
            TM_WARN("RenderShaderToPixels: failed to create RT texture %dx%d", pixelW, pixelH);
            return false;
        }
        hr = m_device->CreateRenderTargetView(m_shaderRTTex, nullptr, &m_shaderRTV);
        if (FAILED(hr) || !m_shaderRTV) {
            TM_WARN("RenderShaderToPixels: failed to create RTV %dx%d", pixelW, pixelH);
            return false;
        }

        D3D11_TEXTURE2D_DESC readDesc = rtDesc;
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.BindFlags = 0;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = m_device->CreateTexture2D(&readDesc, nullptr, &m_shaderReadbackTex);
        if (FAILED(hr) || !m_shaderReadbackTex) {
            TM_WARN("RenderShaderToPixels: failed to create readback texture %dx%d", pixelW, pixelH);
            return false;
        }

        m_shaderRTW = pixelW;
        m_shaderRTH = pixelH;
    }

    // Backup render target and viewport so the offscreen pass does not disturb the main frame.
    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    m_context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    UINT oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    m_context->RSGetViewports(&oldViewportCount, oldViewports);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)pixelW;
    vp.Height = (FLOAT)pixelH;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    m_context->OMSetRenderTargets(1, &m_shaderRTV, nullptr);
    m_context->RSSetViewports(1, &vp);

    const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    m_context->ClearRenderTargetView(m_shaderRTV, clear);

    ShaderUniforms offscreenUniforms = uniforms;
    offscreenUniforms.iResolution[0] = (float)pixelW;
    offscreenUniforms.iResolution[1] = (float)pixelH;
    DrawShaderInRect(shaderIndex, 0.0f, 0.0f, 1.0f, 1.0f, offscreenUniforms);

    // Unbind the RT before copying to the readback texture.
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_context->CopyResource(m_shaderReadbackTex, m_shaderRTTex);

    outPixels.resize((size_t)pixelW * (size_t)pixelH * 4);
    D3D11_MAPPED_SUBRESOURCE map = {};
    bool ok = false;
    if (SUCCEEDED(m_context->Map(m_shaderReadbackTex, 0, D3D11_MAP_READ, 0, &map))) {
        for (int y = 0; y < pixelH; ++y) {
            memcpy(outPixels.data() + (size_t)y * pixelW * 4,
                   (const BYTE*)map.pData + (size_t)y * map.RowPitch,
                   (size_t)pixelW * 4);
        }
        m_context->Unmap(m_shaderReadbackTex, 0);
        ok = true;
    }

    m_context->OMSetRenderTargets(oldRTV ? 1u : 0u, oldRTV ? &oldRTV : nullptr, oldDSV);
    if (oldViewportCount > 0) {
        m_context->RSSetViewports(oldViewportCount, oldViewports);
    }

    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();

    return ok;
}

void TmRenderer::Shutdown()
{
    ReleaseAll();
    m_device = nullptr;
    m_context = nullptr;
    m_ready = false;
}
