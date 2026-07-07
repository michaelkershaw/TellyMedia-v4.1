#pragma once
#include "tm/TmPlatform.h"

// ─── Modern color system (depth hierarchy) ───────────────────────────────────
namespace TmColor {
    const COLORREF BG_DEEP     = RGB(10, 10, 14);
    const COLORREF BG_BASE     = RGB(16, 16, 20);
    const COLORREF BG_PANEL    = RGB(22, 22, 28);
    const COLORREF BG_CARD     = RGB(32, 32, 40);
    const COLORREF BG_ELEVATED = RGB(42, 42, 52);

    const COLORREF ACCENT       = RGB(0, 120, 215);
    const COLORREF ACCENT_HOVER = RGB(32, 152, 255);
    const COLORREF ACCENT_ACTIVE= RGB(0, 100, 190);
    const COLORREF ACCENT_GLOW  = RGB(64, 180, 255);

    const COLORREF TEXT_PRIMARY  = RGB(255, 255, 255);
    const COLORREF TEXT_SECOND   = RGB(200, 202, 215);
    const COLORREF TEXT_THIRD    = RGB(140, 142, 155);
    const COLORREF TEXT_DISABLED = RGB(90, 92, 105);

    const COLORREF HOVER    = RGB(48, 48, 60);
    const COLORREF ACTIVE   = RGB(58, 58, 72);
    const COLORREF SELECTED = ACCENT;
    const COLORREF BORDER   = RGB(58, 58, 70);

    const COLORREF OK     = RGB(46, 204, 113);
    const COLORREF WARN   = RGB(241, 196, 15);
    const COLORREF ERR    = RGB(231, 76, 60);
}

// ─── 8px spacing grid ─────────────────────────────────────────────────────────
namespace TmSpace {
    const int XXS = 4, XS = 8, SM = 12, MD = 16, LG = 24, XL = 32, XXL = 48;
}

// ─── Border radius scale ──────────────────────────────────────────────────────
namespace TmRadius {
    const int SM = 4, MD = 8, LG = 12, XL = 16;
}

// ─── DPI helpers (per-window scaling) ─────────────────────────────────────────
namespace TmDpi {
    float Scale(HWND hwnd);          // current scale factor (1.0 == 96 DPI)
    int   Px(HWND hwnd, int logical); // scale a logical px value
}

// ─── Responsive layout metrics ────────────────────────────────────────────────
// Recomputed on every WM_SIZE so the UI reflows fluidly at any window size.
struct LayoutMetrics {
    int   windowW, windowH;
    float dpi;
    int   headerH;
    int   tabBarH;
    int   statusH;
    int   sidebarW;
    int   contentX, contentY, contentW, contentH;
    int   gridCols;
    int   slotSize;
    int   slotGap;
    bool  compact;

    void Calculate(int w, int h, float dpiScale);
};

// ─── Shared fonts (created once, scaled to DPI) ───────────────────────────────
struct TmFonts {
    HFONT title;
    HFONT heading;
    HFONT body;
    HFONT caption;
    HFONT bold;
    void Create(float dpi);
    void Destroy();
};

// ─── Drawing primitives (GDI, double-buffer friendly) ────────────────────────
namespace TmDraw {
    void FillRoundRect(HDC hdc, RECT rc, COLORREF fill, int radius);
    void StrokeRoundRect(HDC hdc, RECT rc, COLORREF stroke, int radius, int width);
    void GradientV(HDC hdc, RECT rc, COLORREF top, COLORREF bottom);
    void Card(HDC hdc, RECT rc, COLORREF bg, int radius, bool glow);
    void TextLeft(HDC hdc, RECT rc, const wchar_t* s, COLORREF c, HFONT f);
    void TextCenter(HDC hdc, RECT rc, const wchar_t* s, COLORREF c, HFONT f);
    COLORREF Lerp(COLORREF a, COLORREF b, float t);
}
