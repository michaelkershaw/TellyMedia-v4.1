#include "tm/TmTheme.h"

// ─── DPI ──────────────────────────────────────────────────────────────────────
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

float TmDpi::Scale(HWND hwnd)
{
    static PFN_GetDpiForWindow pGetDpi = nullptr;
    static bool resolved = false;
    if (!resolved) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) pGetDpi = (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow");
        resolved = true;
    }
    UINT dpi = 96;
    if (pGetDpi && hwnd) dpi = pGetDpi(hwnd);
    else {
        HDC dc = GetDC(hwnd);
        if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(hwnd, dc); }
    }
    if (dpi == 0) dpi = 96;
    return (float)dpi / 96.0f;
}

int TmDpi::Px(HWND hwnd, int logical)
{
    return (int)(logical * Scale(hwnd) + 0.5f);
}

// ─── LayoutMetrics: responsive reflow ────────────────────────────────────────
void LayoutMetrics::Calculate(int w, int h, float dpiScale)
{
    windowW = w; windowH = h; dpi = dpiScale;
    auto S = [&](int v){ return (int)(v * dpi + 0.5f); };

    headerH = S(64);
    tabBarH = S(0);          // tabs live inside header
    statusH = S(26);

    // Responsive breakpoints based on logical width.
    float logicalW = w / dpi;
    if (logicalW < 1100) {
        compact   = true;
        gridCols  = 5;
        slotSize  = S(118);
        sidebarW  = S(220);
    } else if (logicalW < 1550) {
        compact   = false;
        gridCols  = 7;
        slotSize  = S(138);
        sidebarW  = S(260);
    } else {
        compact   = false;
        gridCols  = 9;
        slotSize  = S(150);
        sidebarW  = S(290);
    }
    slotGap = S(8);

    contentX = S(TmSpace::MD);
    contentY = headerH + S(TmSpace::SM);
    contentW = w - S(TmSpace::MD) * 2;
    contentH = h - contentY - statusH - S(TmSpace::SM);
    if (contentW < S(200)) contentW = S(200);
    if (contentH < S(200)) contentH = S(200);
}

// ─── Fonts ──────────────────────────────────────────────────────────────────
static HFONT MakeFont(const wchar_t* face, int px, int weight)
{
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void TmFonts::Create(float dpi)
{
    Destroy();
    auto S = [&](int v){ return (int)(v * dpi + 0.5f); };
    const wchar_t* face = L"Segoe UI";
    title   = MakeFont(face, S(20), FW_SEMIBOLD);
    heading = MakeFont(face, S(15), FW_SEMIBOLD);
    body    = MakeFont(face, S(13), FW_NORMAL);
    caption = MakeFont(face, S(11), FW_NORMAL);
    bold    = MakeFont(face, S(13), FW_BOLD);
}

void TmFonts::Destroy()
{
    if (title)   { DeleteObject(title);   title = nullptr; }
    if (heading) { DeleteObject(heading); heading = nullptr; }
    if (body)    { DeleteObject(body);    body = nullptr; }
    if (caption) { DeleteObject(caption); caption = nullptr; }
    if (bold)    { DeleteObject(bold);    bold = nullptr; }
}

// ─── Drawing primitives ───────────────────────────────────────────────────────
void TmDraw::FillRoundRect(HDC hdc, RECT rc, COLORREF fill, int radius)
{
    HBRUSH b = CreateSolidBrush(fill);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, b);
    HPEN p = CreatePen(PS_NULL, 0, 0);
    HPEN op = (HPEN)SelectObject(hdc, p);
    int d = radius * 2;
    if (d <= 0) FillRect(hdc, &rc, b);
    else RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, d, d);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(p);
}

void TmDraw::StrokeRoundRect(HDC hdc, RECT rc, COLORREF stroke, int radius, int width)
{
    HPEN p = CreatePen(PS_SOLID, width, stroke);
    HPEN op = (HPEN)SelectObject(hdc, p);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    int d = radius * 2;
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, d, d);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(p);
}

void TmDraw::GradientV(HDC hdc, RECT rc, COLORREF top, COLORREF bottom)
{
    int h = rc.bottom - rc.top;
    if (h <= 0) return;
    int tr = GetRValue(top), tg = GetGValue(top), tb = GetBValue(top);
    int br = GetRValue(bottom), bg = GetGValue(bottom), bb = GetBValue(bottom);
    for (int y = 0; y < h; ++y) {
        float t = (float)y / (float)h;
        int r = tr + (int)((br - tr) * t);
        int g = tg + (int)((bg - tg) * t);
        int b = tb + (int)((bb - tb) * t);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN op = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left, rc.top + y, nullptr);
        LineTo(hdc, rc.right, rc.top + y);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
}

void TmDraw::Card(HDC hdc, RECT rc, COLORREF bg, int radius, bool glow)
{
    if (glow) {
        RECT g = rc; InflateRect(&g, 2, 2);
        StrokeRoundRect(hdc, g, TmColor::ACCENT_GLOW, radius + 2, 1);
    }
    FillRoundRect(hdc, rc, bg, radius);
    StrokeRoundRect(hdc, rc, TmColor::BORDER, radius, 1);
}

void TmDraw::TextLeft(HDC hdc, RECT rc, const wchar_t* s, COLORREF c, HFONT f)
{
    int om = SetBkMode(hdc, TRANSPARENT);
    COLORREF oc = SetTextColor(hdc, c);
    HFONT of = (HFONT)SelectObject(hdc, f);
    DrawTextW(hdc, s, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, of); SetTextColor(hdc, oc); SetBkMode(hdc, om);
}

void TmDraw::TextCenter(HDC hdc, RECT rc, const wchar_t* s, COLORREF c, HFONT f)
{
    int om = SetBkMode(hdc, TRANSPARENT);
    COLORREF oc = SetTextColor(hdc, c);
    HFONT of = (HFONT)SelectObject(hdc, f);
    DrawTextW(hdc, s, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, of); SetTextColor(hdc, oc); SetBkMode(hdc, om);
}

COLORREF TmDraw::Lerp(COLORREF a, COLORREF b, float t)
{
    if (t < 0) t = 0; if (t > 1) t = 1;
    int r = GetRValue(a) + (int)((GetRValue(b) - GetRValue(a)) * t);
    int g = GetGValue(a) + (int)((GetGValue(b) - GetGValue(a)) * t);
    int bl= GetBValue(a) + (int)((GetBValue(b) - GetBValue(a)) * t);
    return RGB(r, g, bl);
}
