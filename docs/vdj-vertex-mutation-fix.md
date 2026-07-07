# VDJ Main Panel Video Positioning Fix

## Problem
When the Main Panel in TellyMedia is resized or moved, the VDJ native video output
on the live screen remained fullscreen instead of following the panel bounds.

## Root Cause

### Attempt 1 — Custom DrawRectWithTexture (failed)
We tried drawing VDJ's deck texture ourselves using our own D3D11 pipeline
(`DrawRectWithTexture` with NDC coordinates). This appeared to work internally
but the video stayed fullscreen on the VDJ output.

**Why it failed:** VDJ uses a ping-pong (double/triple) render target buffer.
Our draw calls were landing on a different render target than the one VDJ
composites and presents to the screen. The RTV pointer we obtained via
`OMGetRenderTargets` was valid but not the final output buffer.

### Attempt 2 — ClearRenderTargetView + custom draw (failed)
We tried clearing the bound RTV to red/black as a diagnostic. No colour
appeared on screen, confirming our draws were hitting the wrong buffer.

### Root Fix — Vertex Mutation + DrawDeck()
**Key insight:** `GetTexture(VdjVideoEngineDirectX11, &srv, &vtx)` returns a
pointer to VDJ's own internal `TVertex` array. When `DrawDeck()` is called,
VDJ reads those same vertices to position the video on its own output buffer.

By **mutating the vertex positions before calling `DrawDeck()`** and restoring
them afterward, VDJ's own compositor draws the video exactly where we tell it
to — into the correct buffer, with no pipeline conflicts.

## Critical Detail: VDJ Uses Pixel-Space Coordinates

**VDJ's `TVertex` positions are in pixel space, NOT NDC.**

From logged original vertices on a 958×535 output:
```
[0](0.00, 0.00)    // Top-Left
[1](958.00, 0.00)  // Top-Right
[2](958.00, 535.00)// Bottom-Right
[3](0.00, 535.00)  // Bottom-Left
```

Winding order: `TL, TR, BR, BL`

NDC values like `-0.659` or `0.925` placed into these slots render a ~1px
quad in the top-left corner — effectively invisible.

## The Fix (TmPlugin.cpp — OnDraw)

```cpp
// Convert normalised panel coords to VDJ pixel-space coords
float outW = (float)width;
float outH = (float)height;
float pxX1 = mainX * outW;
float pxY1 = mainY * outH;
float pxX2 = (mainX + mainW) * outW;
float pxY2 = (mainY + mainH) * outH;

if (hasMainPanel && deckVtx) {
    // Save original positions
    float origX[4], origY[4];
    for (int i = 0; i < 4; ++i) {
        origX[i] = deckVtx[i].position.x;
        origY[i] = deckVtx[i].position.y;
    }

    // Overwrite with panel pixel bounds (winding: TL, TR, BR, BL)
    deckVtx[0].position.x = pxX1; deckVtx[0].position.y = pxY1;
    deckVtx[1].position.x = pxX2; deckVtx[1].position.y = pxY1;
    deckVtx[2].position.x = pxX2; deckVtx[2].position.y = pxY2;
    deckVtx[3].position.x = pxX1; deckVtx[3].position.y = pxY2;

    DrawDeck(); // VDJ composites into its own correct output buffer

    // Restore so VDJ's internal bookkeeping stays intact
    for (int i = 0; i < 4; ++i) {
        deckVtx[i].position.x = origX[i];
        deckVtx[i].position.y = origY[i];
    }
}
```

## Key Takeaways for Future VDJ Plugin Development

1. **Never fight VDJ's compositor** — use `DrawDeck()` for all deck video
   output. Custom D3D11 draws target the wrong render target.

2. **VDJ vertex coords are pixel-space**, not NDC. Use `width` and `height`
   (members of `IVdjPluginVideoFx8`) to scale normalised coords.

3. **Vertex winding order** for VDJ's 4-vertex strip: `[0]=TL [1]=TR [2]=BR [3]=BL`.

4. **`GetTexture()` returns a live pointer** to VDJ's internal vertex buffer.
   Mutations take effect immediately on the next `DrawDeck()` call.
   Always restore originals after calling `DrawDeck()`.

5. **`ClearRenderTargetView` on the bound RTV** may clear a buffer that is
   not the final presented output. Avoid relying on it for background fills
   when `DrawDeck()` is in use — VDJ manages the background itself.
