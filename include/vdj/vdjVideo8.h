////////////////////////////////////////////////////////////////////////////
// VirtualDJ - Plugin SDK
// (c)Atomix Productions 2011-2019
// Cross-platform (Windows D3D11 + macOS OpenGL)
////////////////////////////////////////////////////////////////////////////
#ifndef VdjVideo8H
#define VdjVideo8H

#include "vdjPlugin8.h"

#if defined(VDJ_MAC)
// macOS: OpenGL
#include <OpenGL/gl.h>
#include <OpenGL/CGLCurrent.h>
// GLuint and CGLContextObj are defined by the OpenGL headers above
#elif defined(VDJ_WIN)
// Windows: Direct3D 11
#include <d3d11.h>
#endif

#ifndef TVertex
struct TVertex {
    struct { float x, y, z; } position;
    DWORD color;
    float tu, tv;
};
#endif

enum EVdjVideoEngine {
    VdjVideoEngineAny       = 0,
    VdjVideoEngineDirectX9  = 1,
    VdjVideoEngineOpenGL    = 2,
    VdjVideoEngineDirectX11 = 3,
    VdjVideoEngineOpenGLES2 = 4
};

#define VDJFLAG_VIDEO_MASTERONLY        0x10000
#define VDJFLAG_VIDEO_VISUALISATION     0x20000
#define VDJFLAG_VIDEO_OVERLAY           0x40000
#define VDJFLAG_VIDEO_HASRESIZE         0x80000
#define VDJFLAG_VIDEO_NOAUTOACTIVE      0x200000
#define VDJFLAG_VIDEO_OUTPUTRESOLUTION  0x400000
#define VDJFLAG_VIDEO_OUTPUTASPECTRATIO 0x800000

struct IVdjVideoCallbacks8 {
    virtual HRESULT DrawDeck() = 0;
    virtual HRESULT GetDevice(EVdjVideoEngine engine, void **device) = 0;
    virtual HRESULT GetTexture(EVdjVideoEngine engine, void **texture, TVertex **vertices) = 0;
};

////////////////////////////////////////////////////////////////////////////
// VideoFx plugin class
class IVdjPluginVideoFx8 : public IVdjPlugin8 {
public:
    virtual HRESULT VDJ_API OnStart()  { return S_OK; }
    virtual HRESULT VDJ_API OnStop()   { return S_OK; }
    virtual HRESULT VDJ_API OnDraw() = 0;

    HRESULT GetDevice(EVdjVideoEngine engine, void **device)                         { return vcb->GetDevice(engine, device); }
    HRESULT GetTexture(EVdjVideoEngine engine, void **texture, TVertex **vertices)   { return vcb->GetTexture(engine, texture, vertices); }
    HRESULT DrawDeck()                                                                { return vcb->DrawDeck(); }

    virtual HRESULT VDJ_API OnDeviceInit()  { return S_OK; }
    virtual HRESULT VDJ_API OnDeviceClose() { return S_OK; }
    virtual HRESULT VDJ_API OnAudioSamples(float *buffer, int nb) { return E_NOTIMPL; }

    int SampleRate;
    int SongBpm;
    double SongPosBeats;
    int width, height;
    IVdjVideoCallbacks8 *vcb;
};

////////////////////////////////////////////////////////////////////////////
// GUID definitions
#ifndef VDJVIDEO8GUID_DEFINED
#define VDJVIDEO8GUID_DEFINED
static const GUID IID_IVdjPluginVideoFx8              = { 0xbf1876aa, 0x3cbd, 0x404a, { 0xbe, 0xab, 0x5f, 0x8b, 0x51, 0xe3, 0x90, 0xc0 } };
static const GUID IID_IVdjPluginVideoTransition8      = { 0x2f350983, 0xf88f, 0x429c, { 0x87, 0x75, 0x62, 0x87, 0x68, 0x7d, 0xe0, 0xd7 } };
static const GUID IID_IVdjPluginVideoTransitionMultiDeck8 = { 0x54d0e81c, 0x51a6, 0x49b0, { 0x82, 0x3f, 0x75, 0x91, 0x76, 0xf1, 0xcf, 0x06 } };
#endif

#endif
