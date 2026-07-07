// TmRendererMac.mm — OpenGL renderer for macOS
// Replaces the D3D11 TmRenderer.cpp. Uses OpenGL 2.1+ (legacy profile, which
// is what VirtualDJ provides on macOS for video FX plugins).

#if defined(VDJ_MAC)

#import <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLCurrent.h>
#include "tm/TmRenderer.h"
#include "tm/TmLogger.h"
#include <string>
#include <vector>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

// ─── GLSL shaders ────────────────────────────────────────────────────────────

// Vertex shader for textured quads (matches VDJ TVertex format)
static const char* kVS = R"GLSL(
attribute vec3 aPos;
attribute vec4 aColor;
attribute vec2 aUV;
varying vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vUV = aUV;
}
)GLSL";

// Fragment shader for textured quad with alpha
static const char* kFS = R"GLSL(
uniform sampler2D uTex;
uniform float uAlpha;
uniform float uUseTexAlpha;
varying vec2 vUV;
void main() {
    vec4 c = texture2D(uTex, vUV);
    // uUseTexAlpha=1: respect texture alpha (overlay panels)
    // uUseTexAlpha=0: force opaque (deck/media - VDJ render targets often have alpha=0)
    c.a = mix(uAlpha, c.a * uAlpha, uUseTexAlpha);
    gl_FragColor = c;
}
)GLSL";

// Crossfade fragment shader: blends current frame with previous frame
static const char* kCrossfadeFS = R"GLSL(
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform float uAlpha;
uniform float uMix;
varying vec2 vUV;
void main() {
    vec4 c0 = texture2D(uTex0, vUV);
    vec4 c1 = texture2D(uTex1, vUV);
    vec4 c = mix(c1, c0, uMix);
    c.a *= uAlpha;
    gl_FragColor = c;
}
)GLSL";

// Fullscreen vertex shader for custom shaders (generates quad from gl_VertexID)
// OpenGL 2.1 doesn't have gl_VertexID, so we use a VBO with 6 verts
static const char* kShaderVS = R"GLSL(
attribute vec2 aPos;
attribute vec2 aUV;
varying vec2 vUV;
uniform vec4 uPanel; // x, y, w, h in 0..1
void main() {
    vec2 pos = vec2(uPanel.x + aUV.x * uPanel.w, uPanel.y + aUV.y * uPanel.h) * 2.0 - 1.0;
    pos.y = -pos.y; // Flip Y for OpenGL
    gl_Position = vec4(pos, 0.0, 1.0);
    vUV = aUV;
}
)GLSL";

// ─── GL helper functions ─────────────────────────────────────────────────────

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        TM_ERROR("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint LinkProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    if (!vs) return 0;
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        TM_ERROR("Program link error: %s", log);
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ─── Quad vertex data ────────────────────────────────────────────────────────
struct QuadVertex { float x, y, z; DWORD color; float u, v; };

static const QuadVertex kFullscreenQuad[] = {
    { -1.f, -1.f, 0.f, 0xFFFFFFFF, 0.f, 0.f },
    {  1.f, -1.f, 0.f, 0xFFFFFFFF, 1.f, 0.f },
    { -1.f,  1.f, 0.f, 0xFFFFFFFF, 0.f, 1.f },
    {  1.f,  1.f, 0.f, 0xFFFFFFFF, 1.f, 1.f },
};

// Triangle strip: 4 verts = 2 triangles
static const GLushort kQuadIndices[] = { 0, 1, 2, 3 };

// ─── Constructor / Destructor ────────────────────────────────────────────────

TmRenderer::TmRenderer()
    : m_glContext(nullptr), m_frameTex(0), m_prevFrameTex(0), m_overlayTex(0),
      m_vao(0), m_vb(0), m_shaderProg(0), m_crossfadeProg(0), m_sampler(0),
      m_texW(0), m_texH(0), m_prevTexW(0), m_prevTexH(0),
      m_ready(false), m_hasPrevFrame(false), m_shaderCount(0),
      m_shaderUBO(0), m_shaderFBO(0), m_shaderRTTex(0),
      m_shaderRTW(0), m_shaderRTH(0)
{
    ZeroMemory(m_shaders, sizeof(m_shaders));
}

TmRenderer::~TmRenderer() { Shutdown(); }

// ─── Init / Shutdown ─────────────────────────────────────────────────────────

bool TmRenderer::Init(CGLContextObj ctx) {
    if (!ctx) return false;
    m_glContext = ctx;

    CGLContextObj prevCtx = CGLGetCurrentContext();
    CGLSetCurrentContext(ctx);

    // Compile shaders
    m_shaderProg = LinkProgram(kVS, kFS);
    if (!m_shaderProg) { TM_ERROR("Failed to compile main shader program"); return false; }

    m_crossfadeProg = LinkProgram(kVS, kCrossfadeFS);
    if (!m_crossfadeProg) { TM_ERROR("Failed to compile crossfade program"); }

    // Create vertex buffer for fullscreen quad
    glGenBuffers(1, &m_vb);
    glBindBuffer(GL_ARRAY_BUFFER, m_vb);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenQuad), kFullscreenQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Create textures
    glGenTextures(1, &m_frameTex);
    glGenTextures(1, &m_prevFrameTex);
    glGenTextures(1, &m_overlayTex);

    // Configure textures
    for (GLuint tex : {m_frameTex, m_prevFrameTex, m_overlayTex}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Create FBO for shader rendering
    glGenFramebuffers(1, &m_shaderFBO);
    glGenTextures(1, &m_shaderRTTex);

    m_ready = true;
    TM_INFO("TmRenderer (OpenGL) initialized");

    CGLSetCurrentContext(prevCtx);
    return true;
}

void TmRenderer::Shutdown() {
    if (!m_glContext) return;
    CGLContextObj prevCtx = CGLGetCurrentContext();
    CGLSetCurrentContext(m_glContext);

    ReleaseAll();

    CGLSetCurrentContext(prevCtx);
    m_glContext = nullptr;
    m_ready = false;
}

void TmRenderer::ReleaseAll() {
    if (m_frameTex) { glDeleteTextures(1, &m_frameTex); m_frameTex = 0; }
    if (m_prevFrameTex) { glDeleteTextures(1, &m_prevFrameTex); m_prevFrameTex = 0; }
    if (m_overlayTex) { glDeleteTextures(1, &m_overlayTex); m_overlayTex = 0; }
    if (m_vb) { glDeleteBuffers(1, &m_vb); m_vb = 0; }
    if (m_shaderProg) { glDeleteProgram(m_shaderProg); m_shaderProg = 0; }
    if (m_crossfadeProg) { glDeleteProgram(m_crossfadeProg); m_crossfadeProg = 0; }
    if (m_shaderFBO) { glDeleteFramebuffers(1, &m_shaderFBO); m_shaderFBO = 0; }
    if (m_shaderRTTex) { glDeleteTextures(1, &m_shaderRTTex); m_shaderRTTex = 0; }

    // Release shader programs
    for (int i = 0; i < m_shaderCount; i++) {
        if (m_shaders[i].prog) {
            glDeleteProgram(m_shaders[i].prog);
            m_shaders[i].prog = 0;
        }
    }
    m_shaderCount = 0;
}

// ─── Frame texture ───────────────────────────────────────────────────────────

bool TmRenderer::EnsureFrameTexture(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (w == m_texW && h == m_texH && m_frameTex) return true;

    m_texW = w;
    m_texH = h;

    glBindTexture(GL_TEXTURE_2D, m_frameTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    TM_INFO("Frame texture created: %dx%d", w, h);
    return true;
}

void TmRenderer::UploadFrame(const void* bgra, int w, int h, int stride) {
    if (!m_ready || !bgra || w <= 0 || h <= 0) return;

    EnsureFrameTexture(w, h);

    // If stride == w*4, we can upload directly. Otherwise we need to repack.
    glBindTexture(GL_TEXTURE_2D, m_frameTex);

    if (stride == w * 4) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    } else {
        // Repack rows
        std::vector<BYTE> packed(w * h * 4);
        const BYTE* src = (const BYTE*)bgra;
        for (int y = 0; y < h; y++) {
            memcpy(&packed[y * w * 4], src + y * stride, w * 4);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, packed.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    // Save a CPU copy for crossfade
    m_currFrameData.resize(w * h * 4);
    if (stride == w * 4) {
        memcpy(m_currFrameData.data(), bgra, w * h * 4);
    } else {
        const BYTE* src = (const BYTE*)bgra;
        for (int y = 0; y < h; y++) {
            memcpy(&m_currFrameData[y * w * 4], src + y * stride, w * 4);
        }
    }
}

void TmRenderer::SaveCurrentAsPrevious() {
    if (!m_ready || m_currFrameData.empty()) return;
    if (m_texW <= 0 || m_texH <= 0) return;

    m_prevTexW = m_texW;
    m_prevTexH = m_texH;

    glBindTexture(GL_TEXTURE_2D, m_prevFrameTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_prevTexW, m_prevTexH, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, m_currFrameData.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_hasPrevFrame = true;
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

void TmRenderer::SetFullscreenQuad() {
    glBindBuffer(GL_ARRAY_BUFFER, m_vb);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenQuad), kFullscreenQuad, GL_STATIC_DRAW);
}


void TmRenderer::DrawFullscreen(float alpha) {
    if (!m_ready || !m_frameTex) return;

    glUseProgram(m_shaderProg);

    // Set uniforms
    GLint alphaLoc = glGetUniformLocation(m_shaderProg, "uAlpha");
    GLint useTexAlphaLoc = glGetUniformLocation(m_shaderProg, "uUseTexAlpha");
    GLint texLoc = glGetUniformLocation(m_shaderProg, "uTex");

    glUniform1f(alphaLoc, alpha);
    glUniform1f(useTexAlphaLoc, 0.0f); // Force opaque for media
    glUniform1i(texLoc, 0);

    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_frameTex);

    // Bind VBO and set up attribs
    SetFullscreenQuad();
    GLint posLoc = glGetAttribLocation(m_shaderProg, "aPos");
    GLint uvLoc = glGetAttribLocation(m_shaderProg, "aUV");
    if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, x));
    }
    if (uvLoc >= 0) {
        glEnableVertexAttribArray(uvLoc);
        glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, u));
    }

    // Draw quad as triangle strip
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glUseProgram(0);
}

void TmRenderer::DrawCrossfade(float alpha, float crossfadeProgress) {
    if (!m_ready || !m_frameTex || !m_hasPrevFrame || !m_crossfadeProg) {
        DrawFullscreen(alpha);
        return;
    }

    glUseProgram(m_crossfadeProg);

    GLint alphaLoc = glGetUniformLocation(m_crossfadeProg, "uAlpha");
    GLint mixLoc = glGetUniformLocation(m_crossfadeProg, "uMix");
    GLint tex0Loc = glGetUniformLocation(m_crossfadeProg, "uTex0");
    GLint tex1Loc = glGetUniformLocation(m_crossfadeProg, "uTex1");

    glUniform1f(alphaLoc, alpha);
    glUniform1f(mixLoc, crossfadeProgress);
    glUniform1i(tex0Loc, 0);
    glUniform1i(tex1Loc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_frameTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_prevFrameTex);

    SetFullscreenQuad();
    GLint posLoc = glGetAttribLocation(m_crossfadeProg, "aPos");
    GLint uvLoc = glGetAttribLocation(m_crossfadeProg, "aUV");
    if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, x));
    }
    if (uvLoc >= 0) {
        glEnableVertexAttribArray(uvLoc);
        glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, u));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

// ─── Overlay ─────────────────────────────────────────────────────────────────

void TmRenderer::UploadOverlay(const void* bgra, int w, int h, int stride) {
    if (!m_ready || !bgra || w <= 0 || h <= 0) return;

    glBindTexture(GL_TEXTURE_2D, m_overlayTex);
    if (stride == w * 4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    } else {
        std::vector<BYTE> packed(w * h * 4);
        const BYTE* src = (const BYTE*)bgra;
        for (int y = 0; y < h; y++) {
            memcpy(&packed[y * w * 4], src + y * stride, w * 4);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, packed.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TmRenderer::DrawOverlay() {
    if (!m_ready || !m_overlayTex) return;

    glUseProgram(m_shaderProg);

    GLint alphaLoc = glGetUniformLocation(m_shaderProg, "uAlpha");
    GLint useTexAlphaLoc = glGetUniformLocation(m_shaderProg, "uUseTexAlpha");
    GLint texLoc = glGetUniformLocation(m_shaderProg, "uTex");

    glUniform1f(alphaLoc, 1.0f);
    glUniform1f(useTexAlphaLoc, 1.0f); // Respect texture alpha for overlay
    glUniform1i(texLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_overlayTex);

    SetFullscreenQuad();
    GLint posLoc = glGetAttribLocation(m_shaderProg, "aPos");
    GLint uvLoc = glGetAttribLocation(m_shaderProg, "aUV");
    if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, x));
    }
    if (uvLoc >= 0) {
        glEnableVertexAttribArray(uvLoc);
        glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, u));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glUseProgram(0);
}

// ─── Rect drawing ────────────────────────────────────────────────────────────

void TmRenderer::DrawRect(float x, float y, float w, float h, float alpha) {
    DrawRectWithTexture(x, y, w, h, alpha, m_frameTex);
}

void TmRenderer::DrawRectWithTexture(float x, float y, float w, float h, float alpha,
                                      GLuint tex, float u0, float v0, float u1, float v1) {
    if (!m_ready || !tex) return;

    // Build quad vertices for this rect
    QuadVertex verts[4] = {
        { x * 2.f - 1.f, -(y * 2.f - 1.f),     0.f, 0xFFFFFFFF, u0, v0 },
        { (x + w) * 2.f - 1.f, -(y * 2.f - 1.f), 0.f, 0xFFFFFFFF, u1, v0 },
        { x * 2.f - 1.f, -((y + h) * 2.f - 1.f), 0.f, 0xFFFFFFFF, u0, v1 },
        { (x + w) * 2.f - 1.f, -((y + h) * 2.f - 1.f), 0.f, 0xFFFFFFFF, u1, v1 },
    };

    glUseProgram(m_shaderProg);
    GLint alphaLoc = glGetUniformLocation(m_shaderProg, "uAlpha");
    GLint useTexAlphaLoc = glGetUniformLocation(m_shaderProg, "uUseTexAlpha");
    GLint texLoc = glGetUniformLocation(m_shaderProg, "uTex");
    glUniform1f(alphaLoc, alpha);
    glUniform1f(useTexAlphaLoc, 0.0f);
    glUniform1i(texLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glBindBuffer(GL_ARRAY_BUFFER, m_vb);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    GLint posLoc = glGetAttribLocation(m_shaderProg, "aPos");
    GLint uvLoc = glGetAttribLocation(m_shaderProg, "aUV");
    if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, x));
    }
    if (uvLoc >= 0) {
        glEnableVertexAttribArray(uvLoc);
        glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, u));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glUseProgram(0);
}

void TmRenderer::DrawFullscreenWithTexture(float alpha, GLuint tex) {
    DrawRectWithTexture(0.f, 0.f, 1.f, 1.f, alpha, tex);
}

void TmRenderer::ClearToBlack() {
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// ─── Shader system ───────────────────────────────────────────────────────────

// Read a file into a string
static std::string ReadFile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return "";
    std::string content;
    char buf[4096];
    while (size_t n = fread(buf, 1, sizeof(buf), f)) {
        content.append(buf, n);
    }
    fclose(f);
    return content;
}

// Convert HLSL cbuffer to GLSL uniform
static std::string ConvertShaderToGLSL(const std::string& hlsl) {
    std::string glsl;

    // GLSL version header
    glsl += "#version 120\n";
    glsl += "uniform vec2 iResolution;\n";
    glsl += "uniform float iTime;\n";
    glsl += "uniform float iBeat;\n";
    glsl += "uniform float iLevel;\n";
    glsl += "uniform float iBass;\n";
    glsl += "uniform float iMid;\n";
    glsl += "uniform float iTreble;\n";
    glsl += "uniform float iBpm;\n";
    glsl += "uniform float iSongPosBeats;\n";
    glsl += "varying vec2 vUV;\n\n";

    // Add compatibility defines
    glsl += "#define time iTime\n";
    glsl += "#define resX iResolution.x\n";
    glsl += "#define resY iResolution.y\n";
    glsl += "#define bass iBass\n";
    glsl += "#define mid iMid\n";
    glsl += "#define treble iTreble\n";
    glsl += "#define bpm iBpm\n";
    glsl += "#define beat iBeat\n\n";

    // Strip HLSL cbuffer blocks and replace entry point
    std::string body = hlsl;

    // Remove cbuffer blocks
    size_t cbufPos = body.find("cbuffer");
    while (cbufPos != std::string::npos) {
        size_t openBrace = body.find('{', cbufPos);
        if (openBrace == std::string::npos) break;
        int depth = 1;
        size_t closeBrace = openBrace + 1;
        while (closeBrace < body.length() && depth > 0) {
            if (body[closeBrace] == '{') depth++;
            else if (body[closeBrace] == '}') depth--;
            closeBrace++;
        }
        body.erase(cbufPos, closeBrace - cbufPos);
        cbufPos = body.find("cbuffer");
    }

    // Replace HLSL entry point with GLSL main
    // Common patterns: "float4 main(float2 uv : TEXCOORD0)" etc.
    // For simplicity, wrap the body in a main that outputs gl_FragColor
    // and replace common HLSL types
    size_t mainPos = body.find("float4 main");
    if (mainPos != std::string::npos) {
        // Find the opening brace of main
        size_t bracePos = body.find('{', mainPos);
        if (bracePos != std::string::npos) {
            // Extract the body of main
            int depth = 1;
            size_t endBrace = bracePos + 1;
            while (endBrace < body.length() && depth > 0) {
                if (body[endBrace] == '{') depth++;
                else if (body[endBrace] == '}') depth--;
                endBrace++;
            }
            std::string mainBody = body.substr(bracePos + 1, endBrace - bracePos - 2);

            // Replace HLSL types in the body
            // float4 -> vec4, float2 -> vec2, float3 -> vec3
            // .rgba -> .rgba (already compatible)
            // tex2D(sampler, uv) -> texture2D(sampler, uv)
            std::string converted = mainBody;
            // Simple replacements
            size_t pos = 0;
            while ((pos = converted.find("float4", pos)) != std::string::npos) {
                converted.replace(pos, 6, "vec4");
                pos += 4;
            }
            pos = 0;
            while ((pos = converted.find("float3", pos)) != std::string::npos) {
                converted.replace(pos, 6, "vec3");
                pos += 4;
            }
            pos = 0;
            while ((pos = converted.find("float2", pos)) != std::string::npos) {
                converted.replace(pos, 6, "vec2");
                pos += 4;
            }
            pos = 0;
            while ((pos = converted.find("tex2D", pos)) != std::string::npos) {
                converted.replace(pos, 5, "texture2D");
                pos += 9;
            }
            pos = 0;
            while ((pos = converted.find("lerp(", pos)) != std::string::npos) {
                converted.replace(pos, 4, "mix");
                pos += 3;
            }

            glsl += "void main() {\n";
            glsl += "    vec2 uv = vUV;\n";
            glsl += converted;
            glsl += "\n}\n";
        }
    } else {
        // No main found — just append the body
        glsl += body;
        glsl += "\nvoid main() { gl_FragColor = vec4(0.0); }\n";
    }

    return glsl;
}

bool TmRenderer::LoadShaders(const wchar_t* dllDir) {
    if (!dllDir) return false;

    // Convert wchar_t path to char
    char dir[MAX_PATH];
    wcstombs(dir, dllDir, sizeof(dir));

    // Look for shaders subdirectory
    char shaderDir[MAX_PATH];
    snprintf(shaderDir, sizeof(shaderDir), "%s/shaders", dir);

    DIR* d = opendir(shaderDir);
    if (!d) {
        TM_INFO("No shaders directory found: %s", shaderDir);
        return false;
    }

    m_shaderCount = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr && m_shaderCount < MAX_SHADERS) {
        // Look for .hlsl or .glsl files
        const char* name = entry->d_name;
        size_t nameLen = strlen(name);
        if (nameLen < 6) continue;

        bool isHLSL = (strcmp(name + nameLen - 5, ".hlsl") == 0);
        bool isGLSL = (strcmp(name + nameLen - 5, ".glsl") == 0);
        if (!isHLSL && !isGLSL) continue;

        char fullPath[MAX_PATH];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", shaderDir, name);

        std::string src = ReadFile(fullPath);
        if (src.empty()) continue;

        std::string glslSrc;
        if (isHLSL) {
            glslSrc = ConvertShaderToGLSL(src);
        } else {
            // Already GLSL — add version header
            glslSrc = "#version 120\n";
            glslSrc += "uniform vec2 iResolution;\n";
            glslSrc += "uniform float iTime, iBeat, iLevel, iBass, iMid, iTreble, iBpm, iSongPosBeats;\n";
            glslSrc += "varying vec2 vUV;\n";
            glslSrc += src;
        }

        // Compile as a standalone fragment shader with our vertex shader
        GLuint prog = LinkProgram(kShaderVS, glslSrc.c_str());
        if (prog) {
            // Store shader name (without extension)
            mbstowcs(m_shaders[m_shaderCount].name, name, 63);
            // Strip extension
            wchar_t* dot = wcsrchr(m_shaders[m_shaderCount].name, L'.');
            if (dot) *dot = L'\0';

            m_shaders[m_shaderCount].prog = prog;
            m_shaderCount++;
            TM_INFO("Loaded shader: %s", name);
        } else {
            TM_WARN("Failed to compile shader: %s", name);
        }
    }

    closedir(d);
    TM_INFO("Loaded %d shaders", m_shaderCount);
    return m_shaderCount > 0;
}

void TmRenderer::ReloadShaders(const wchar_t* dllDir) {
    // Release existing shader programs
    for (int i = 0; i < m_shaderCount; i++) {
        if (m_shaders[i].prog) {
            glDeleteProgram(m_shaders[i].prog);
            m_shaders[i].prog = 0;
        }
    }
    m_shaderCount = 0;
    LoadShaders(dllDir);
}

int TmRenderer::GetShaderCount() const { return m_shaderCount; }

const wchar_t* TmRenderer::GetShaderName(int index) const {
    if (index < 0 || index >= m_shaderCount) return L"";
    return m_shaders[index].name;
}

void TmRenderer::DrawShaderInRect(int shaderIndex, float x, float y, float w, float h,
                                   const ShaderUniforms& uniforms) {
    if (!m_ready || shaderIndex < 0 || shaderIndex >= m_shaderCount) return;
    GLuint prog = m_shaders[shaderIndex].prog;
    if (!prog) return;

    glUseProgram(prog);

    // Set uniforms
    GLint loc;
    if ((loc = glGetUniformLocation(prog, "iResolution")) >= 0)
        glUniform2f(loc, uniforms.iResolution[0], uniforms.iResolution[1]);
    if ((loc = glGetUniformLocation(prog, "iTime")) >= 0)
        glUniform1f(loc, uniforms.iTime);
    if ((loc = glGetUniformLocation(prog, "iBeat")) >= 0)
        glUniform1f(loc, uniforms.iBeat);
    if ((loc = glGetUniformLocation(prog, "iLevel")) >= 0)
        glUniform1f(loc, uniforms.iLevel);
    if ((loc = glGetUniformLocation(prog, "iBass")) >= 0)
        glUniform1f(loc, uniforms.iBass);
    if ((loc = glGetUniformLocation(prog, "iMid")) >= 0)
        glUniform1f(loc, uniforms.iMid);
    if ((loc = glGetUniformLocation(prog, "iTreble")) >= 0)
        glUniform1f(loc, uniforms.iTreble);
    if ((loc = glGetUniformLocation(prog, "iBpm")) >= 0)
        glUniform1f(loc, uniforms.iBpm);
    if ((loc = glGetUniformLocation(prog, "iSongPosBeats")) >= 0)
        glUniform1f(loc, uniforms.iSongPosBeats);
    if ((loc = glGetUniformLocation(prog, "uPanel")) >= 0)
        glUniform4f(loc, x, y, w, h);

    // Set up vertex data for fullscreen quad (the VS will scale to panel bounds)
    SetFullscreenQuad();
    GLint posLoc = glGetAttribLocation(prog, "aPos");
    GLint uvLoc = glGetAttribLocation(prog, "aUV");
    if (posLoc >= 0) {
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, x));
    }
    if (uvLoc >= 0) {
        glEnableVertexAttribArray(uvLoc);
        glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                              (void*)offsetof(QuadVertex, u));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glUseProgram(0);
}

bool TmRenderer::RenderShaderToPixels(int shaderIndex, int pixelW, int pixelH,
                                       const ShaderUniforms& uniforms,
                                       std::vector<BYTE>& outPixels) {
    if (!m_ready || shaderIndex < 0 || shaderIndex >= m_shaderCount) return false;
    if (pixelW <= 0 || pixelH <= 0) return false;

    // Save current viewport
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Set up render-to-texture via FBO
    glBindTexture(GL_TEXTURE_2D, m_shaderRTTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixelW, pixelH, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shaderFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_shaderRTTex, 0);

    glViewport(0, 0, pixelW, pixelH);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Render the shader
    ShaderUniforms adjusted = uniforms;
    adjusted.iResolution[0] = (float)pixelW;
    adjusted.iResolution[1] = (float)pixelH;
    DrawShaderInRect(shaderIndex, 0.f, 0.f, 1.f, 1.f, adjusted);

    // Read back pixels
    outPixels.resize(pixelW * pixelH * 4);
    glReadPixels(0, 0, pixelW, pixelH, GL_BGRA, GL_UNSIGNED_BYTE, outPixels.data());

    // Flip vertically (OpenGL is bottom-up, we need top-down)
    std::vector<BYTE> flipped(pixelW * pixelH * 4);
    for (int y = 0; y < pixelH; y++) {
        memcpy(&flipped[y * pixelW * 4], &outPixels[(pixelH - 1 - y) * pixelW * 4], pixelW * 4);
    }
    outPixels = std::move(flipped);

    // Restore
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    m_shaderRTW = pixelW;
    m_shaderRTH = pixelH;
    return true;
}

#endif // VDJ_MAC
