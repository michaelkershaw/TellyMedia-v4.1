#include "tm/TmUI.h"
#include "tm/TmLogger.h"
#include "tm/TmServices.h"
#include "tm/TmMedia.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <objidl.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

namespace TmUI {
    void SaveState(TmUIData* d);
}

static void PaintLicensePage(TmUIData* d, HDC hdc);
static void UpdateLicenseControlLayout(TmUIData* d);

static void EnsureCommonControlsInit()
{
    static bool sInit = false;
    if (sInit) return;

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    sInit = true;
}

static const wchar_t* kClassName = L"TellyMediaV4Window";
static ULONG_PTR       g_gdiToken = 0;
static int             g_gdiRef   = 0;
static Gdiplus::Image* g_logoImage = nullptr;

static Gdiplus::Image* LoadPngFromResource(HINSTANCE hInst, int id)
{
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(id), L"PNG");
    if (!hRes) return nullptr;
    HGLOBAL hData = LoadResource(hInst, hRes);
    if (!hData) return nullptr;
    void* pRes = LockResource(hData);
    DWORD sz = SizeofResource(hInst, hRes);
    if (!pRes || sz == 0) return nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hMem) return nullptr;
    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return nullptr; }
    memcpy(pMem, pRes, sz);
    GlobalUnlock(hMem);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) { GlobalFree(hMem); return nullptr; }
    Gdiplus::Image* img = Gdiplus::Image::FromStream(stream);
    stream->Release();
    if (!img || img->GetLastStatus() != Gdiplus::Ok) { if (img) delete img; return nullptr; }
    return img;
}

// Sidebar control IDs
enum {
    SB_PLAY = 1, SB_PAUSE, SB_STOP,
    SB_SS_START, SB_SS_STOP, SB_DELAY_DEC, SB_DELAY_INC,
    SS_ENABLE, SB_TRANS, SB_DIR, SB_LOOP,
    SB_RENDER_CPU, SB_RENDER_GPU, SB_RENDER_AUTO,
    SB_SCALE_ASPECT, SB_SCALE_STRETCH, SB_SCALE_NOSCALE
};

// Layout-tab control IDs (matching v2)
enum {
    IDC_TM_LAYTAB_CANVAS = 1343,
    IDC_TM_LAYTAB_FIT_ASPECT = 1300,
    IDC_TM_LAYTAB_FIT_STRETCH = 1301,
    IDC_TM_LAYTAB_FIT_NOSCALE = 1302,
    IDC_TM_LAYTAB_BG_BLACK = 1303,
    IDC_TM_LAYTAB_BG_TRANS = 1304,
    IDC_TM_CANVAS_GUIDES = 1305,
    IDC_TM_CANVAS_GRID_SNAP = 1306,
    IDC_TM_LAYTAB_HELP = 1344,
    IDC_TM_LAYTAB_HDR_MAIN = 1345,
    IDC_TM_LAYTAB_MAIN_PANEL_TOGGLE = 1324,
    IDC_TM_LAYTAB_OPEN_TEST_WINDOW = 1342,
    IDC_TM_LAYTAB_HDR_OVERLAY = 1346,
    IDC_TM_LAYTAB_ADD_PANEL = 1321,
    IDC_TM_LAYTAB_ADD_TEXT_IMAGE = 1341,
    IDC_TM_LAYTAB_ADD_TEXT_PANEL = 1325,
    IDC_TM_LAYTAB_ADD_SMS = 1375,
    IDC_TM_LAYTAB_REMOVE_PANEL = 1322,
    IDC_TM_LAYTAB_LOAD_IMAGE = 1323,
    IDC_TM_LAYTAB_PANEL_SELECTOR_LABEL = 1376,
    IDC_TM_LAYTAB_PANEL_LIST = 1378,
    IDC_TM_LAYTAB_REORDER_TOP = 1379,
    IDC_TM_LAYTAB_REORDER_UP = 1380,
    IDC_TM_LAYTAB_REORDER_DOWN = 1381,
    IDC_TM_LAYTAB_REORDER_BOTTOM = 1382,
    IDC_TM_LAYTAB_HDR_TEXT = 1347,
    IDC_TM_LAYTAB_TEXT_ANIM_STATIC = 1326,
    IDC_TM_LAYTAB_TEXT_ANIM_SCROLL = 1327,
    IDC_TM_LAYTAB_TEXT_ANIM_BOUNCE = 1328,
    IDC_TM_LAYTAB_SIZE_LABEL = 1360,
    IDC_TM_LAYTAB_TEXT_SIZE = 1330,
    IDC_TM_LAYTAB_TEXT_COLOR = 1331,
    IDC_TM_LAYTAB_OUTLINE_COLOR = 1334,
    IDC_TM_LAYTAB_OUTLINE_LABEL = 1361,
    IDC_TM_LAYTAB_OUTLINE_SIZE = 1335,
    IDC_TM_LAYTAB_BG_COLOR_BTN = 1332,
    IDC_TM_LAYTAB_BG_TOGGLE = 1333,
    IDC_TM_LAYTAB_OPACITY_LABEL = 1362,
    IDC_TM_LAYTAB_BG_OPACITY = 1336,
    IDC_TM_LAYTAB_HDR_PRESETS = 1348,
    IDC_TM_LAYTAB_PRESET_COMBO = 1349,
    IDC_TM_LAYTAB_PRESET_SAVE = 1350,
    IDC_TM_LAYTAB_PRESET_LOAD = 1351,
    IDC_TM_LAYTAB_PRESET_DEL = 1352,
    IDC_TM_LAYTAB_TEXT_EDIT = 1329
};

// Layout-tab sidebar control IDs (for custom painting)
enum {
    LB_ADD_MAIN = 100, LB_ADD_IMAGE, LB_ADD_TEXT, LB_ADD_SHADER,
    LB_ADD_SONG,
    LB_POS_TL, LB_POS_TC, LB_POS_TR,
    LB_POS_ML, LB_POS_MC, LB_POS_MR,
    LB_POS_BL, LB_POS_BC, LB_POS_BR,
    LB_POS_FULL, LB_Z_UP, LB_Z_DOWN, LB_DELETE,
    LB_LOAD_IMAGE, LB_EDIT_TEXT,
    LB_IMAGE_ANIM_0, LB_IMAGE_ANIM_1, LB_IMAGE_ANIM_2, LB_IMAGE_ANIM_3, LB_IMAGE_ANIM_4,
    LB_TEXT_FONT,
    LB_TEXT_SIZE_DEC, LB_TEXT_SIZE_INC,
    LB_TEXT_COLOR, LB_BG_COLOR,
    LB_DIRECT_SELECT,
    LB_SHADER_PREV, LB_SHADER_PICKER, LB_SHADER_NEXT, LB_SHADER_RELOAD,
    LB_PANEL_SELECT_BASE = 300  // panel list items: 300+index (must not clash with OPT_ = 200)
};

enum {
    IDC_SHADER_PICKER_FILTER = 1500,
    IDC_SHADER_PICKER_LIST = 1501,
    IDC_SHADER_PICKER_INFO = 1502
};

// Options-tab control IDs (for custom painting)
enum {
    OPT_SCALE_ASPECT = 200, OPT_SCALE_STRETCH, OPT_SCALE_NOSCALE,
    OPT_TRANS_CUT, OPT_TRANS_FADE, OPT_TRANS_CROSS,
    OPT_DIR_FWD, OPT_DIR_BACK, OPT_DIR_RANDOM,
    OPT_LOOP_TOGGLE, OPT_DELAY_DEC, OPT_DELAY_INC,
    OPT_SHADER_ENABLE, OPT_SHADER_KARAOKE_DISABLE, OPT_SHADER_PREV, OPT_SHADER_NEXT, OPT_SHADER_RELOAD,
    OPT_KARAOKE_AUTOHIDE,
    OPT_BANKS_EDIT,
    OPT_LICENSE_LOGIN,
    OPT_LICENSE_VALIDATE,
    OPT_LICENSE_ACTIVATE,
    OPT_LICENSE_LOGOUT
};

struct SbButton {
    RECT rc;
    int id;
    wchar_t label[128];
    bool active;
    bool toggle;

    SbButton(RECT r, int buttonId, const wchar_t* text, bool isActive, bool isToggle)
        : rc(r), id(buttonId), active(isActive), toggle(isToggle)
    {
        label[0] = L'\0';
        if (text) wcsncpy_s(label, text, _TRUNCATE);
    }
};

// Forward declarations for layout panel functions
static void PaintLayoutCanvas(TmUIData* d, HDC hdc);
static void BuildLayoutSidebar(TmUIData* d, std::vector<SbButton>& out);
static void HandleLayoutSidebarClick(TmUIData* d, int id);
static RECT PanelRectPx(TmUIData* d, const OverlayPanel& p);
static RECT PanelHandlePx(TmUIData* d, const OverlayPanel& p);
static int PanelAtPoint(TmUIData* d, int px, int py);
static void BuildLayoutShaderRowUnderCanvas(TmUIData* d, std::vector<SbButton>& out);

// Forward declarations for options page functions
static void PaintOptionsPage(TmUIData* d, HDC hdc);
static void BuildOptionsControls(TmUIData* d, std::vector<SbButton>& out);
static void HandleOptionsClick(TmUIData* d, int id);
static bool ShowLicenseLoginDialog(TmUIData* ui);

// Forward declaration for slideshow control used by slot activation helpers
static void StartSlideshow(TmUIData* d);

// ─── small utils ──────────────────────────────────────────────────────────────
static int ScrollBarW(TmUIData* d) { return (int)(12 * d->dpi); }

// Grid scrollbar helpers
static RECT GridScrollTrackRect(TmUIData* d) {
    int sbw = ScrollBarW(d);
    RECT track = { d->rcGrid.right - sbw, d->rcGrid.top, d->rcGrid.right, d->rcGrid.bottom };
    return track;
}

static RECT GridScrollThumbRect(TmUIData* d) {
    RECT track = GridScrollTrackRect(d);
    int viewH = d->rcGrid.bottom - d->rcGrid.top;
    int thumbH = max((int)(30*d->dpi), viewH * viewH / max(1, d->contentHeight));
    int maxScroll = max(0, d->contentHeight - viewH);
    int thumbY = track.top + (maxScroll > 0 ? d->scrollY * (viewH - thumbH) / maxScroll : 0);
    return { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
}

// Panel list scrollbar helpers
static RECT PanelListScrollTrackRect(TmUIData* d) {
    int sbw = ScrollBarW(d);
    RECT track = { d->rcPanelList.right - sbw, d->rcPanelList.top - d->sidebarScrollY, d->rcPanelList.right, d->rcPanelList.bottom - d->sidebarScrollY };
    return track;
}

static RECT PanelListScrollThumbRect(TmUIData* d) {
    RECT track = PanelListScrollTrackRect(d);
    int panelListViewH = d->rcPanelList.bottom - d->rcPanelList.top;
    int panelListContentH = d->numLayoutPanels * (int)(48 * d->dpi + 0.5f) + (int)(16 * d->dpi + 0.5f);
    int thumbH = max((int)(30*d->dpi), panelListViewH * panelListViewH / max(1, panelListContentH));
    int maxScroll = max(0, panelListContentH - panelListViewH);
    int thumbY = track.top + (maxScroll > 0 ? d->panelListScrollY * (panelListViewH - thumbH) / maxScroll : 0);
    return { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
}

// Sidebar scrollbar helpers
static RECT SidebarScrollTrackRect(TmUIData* d) {
    int sbw = ScrollBarW(d);
    RECT sb = d->rcSidebar;
    RECT track = { sb.right - sbw, sb.top, sb.right, sb.bottom };
    return track;
}

static RECT SidebarScrollThumbRect(TmUIData* d) {
    RECT track = SidebarScrollTrackRect(d);
    int viewH = d->rcSidebar.bottom - d->rcSidebar.top;
    int thumbH = max((int)(30*d->dpi), viewH * viewH / max(1, d->sidebarContentHeight));
    int maxScroll = max(0, d->sidebarContentHeight - viewH);
    int thumbY = track.top + (maxScroll > 0 ? d->sidebarScrollY * (viewH - thumbH) / maxScroll : 0);
    return { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
}

static int GridRowsTotal() {
    return (SLOTS_PER_BANK + GRID_COLS - 1) / GRID_COLS;
}

static void StatePath(wchar_t* out, int cap)
{
    wchar_t base[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base);
    wcscat_s(base, MAX_PATH, L"\\VirtualDJ");
    CreateDirectoryW(base, nullptr);
    wcscat_s(base, MAX_PATH, L"\\TellyMedia-v4");
    CreateDirectoryW(base, nullptr);
    swprintf_s(out, cap, L"%s\\state.dat", base);
}

static void BankConfigPath(wchar_t* out, int cap)
{
    wchar_t base[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, base);
    CreateDirectoryW(base, nullptr);
    wcscat_s(base, MAX_PATH, L"\\TellyMedia-reborn");
    CreateDirectoryW(base, nullptr);
    swprintf_s(out, cap, L"%s\\banks.ini", base);
}

static void KaraokePrefsPath(wchar_t* out, int cap)
{
    wchar_t base[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, base);
    CreateDirectoryW(base, nullptr);
    wcscat_s(base, MAX_PATH, L"\\TellyMedia-reborn");
    CreateDirectoryW(base, nullptr);
    swprintf_s(out, cap, L"%s\\settings.ini", base);
}

static COLORREF DefaultBankColor(int index)
{
    static const COLORREF palette[NUM_BANKS] = {
        RGB(88, 101, 242), RGB(34, 197, 94), RGB(245, 158, 11), RGB(236, 72, 153),
        RGB(14, 165, 233), RGB(168, 85, 247), RGB(244, 63, 94), RGB(20, 184, 166),
        RGB(132, 204, 22), RGB(249, 115, 22), RGB(99, 102, 241), RGB(16, 185, 129),
        RGB(234, 88, 12), RGB(148, 163, 184)
    };
    return palette[index % NUM_BANKS];
}

static COLORREF BlendColor(COLORREF a, COLORREF b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int r = (int)(GetRValue(a) * (1.0f - t) + GetRValue(b) * t + 0.5f);
    int g = (int)(GetGValue(a) * (1.0f - t) + GetGValue(b) * t + 0.5f);
    int bl = (int)(GetBValue(a) * (1.0f - t) + GetBValue(b) * t + 0.5f);
    return RGB(r, g, bl);
}

static bool IsLightColor(COLORREF c)
{
    int lum = (int)(GetRValue(c) * 0.299f + GetGValue(c) * 0.587f + GetBValue(c) * 0.114f);
    return lum >= 140;
}

// ─── thumbnail generation ─────────────────────────────────────────────────────
static HBITMAP MakeThumbnail(const wchar_t* path, int size)
{
    // For videos, extract the first frame using Media Foundation.
    if (TmMedia::IsVideoPath(path)) {
        int w = 0, h = 0, stride = 0;
        BYTE* pixels = TmMedia::ExtractFirstFrame(path, &w, &h, &stride);
        if (!pixels) return nullptr;

        // Scale to the thumbnail size.
        float scale = min((float)size / w, (float)size / h);
        int dw = (int)(w * scale), dh = (int)(h * scale);
        int dx = (size - dw) / 2, dy = (size - dh) / 2;

        Bitmap thumb(size, size, PixelFormat32bppARGB);
        Graphics g(&thumb);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.Clear(Color(255, 24, 24, 30));

        // Wrap the BGRA buffer as a Bitmap.
        Bitmap src(w, h, stride, PixelFormat32bppARGB, pixels);
        g.DrawImage(&src, dx, dy, dw, dh);

        free(pixels);

        HBITMAP hbm = nullptr;
        thumb.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
        return hbm;
    }

    // For images, use GDI+ directly.
    Bitmap src(path);
    if (src.GetLastStatus() != Ok) return nullptr;

    Bitmap thumb(size, size, PixelFormat32bppARGB);
    Graphics g(&thumb);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(255, 24, 24, 30));

    int sw = (int)src.GetWidth(), sh = (int)src.GetHeight();
    if (sw <= 0 || sh <= 0) return nullptr;
    float scale = min((float)size / sw, (float)size / sh);
    int dw = (int)(sw * scale), dh = (int)(sh * scale);
    int dx = (size - dw) / 2, dy = (size - dh) / 2;
    g.DrawImage(&src, dx, dy, dw, dh);

    HBITMAP hbm = nullptr;
    thumb.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
    return hbm;
}

static void AssignSlot(TmUIData* d, int bank, int slot, const wchar_t* path)
{
    if (bank < 0 || bank >= NUM_BANKS || slot < 0 || slot >= SLOTS_PER_BANK) return;
    
    // Check license status
    LicenseState st = {};
    TmLicense::Init(&st);
    if (d && d->host && d->host->HostGetLicenseState(&st)) {
        if (st.status != LIC_VALID) {
            TM_WARN("Slot assignment blocked: not licensed");
            MessageBoxW(d->hWnd, L"Please activate a valid license to use media slots.\n\nVisit https://djeventsuite.cloud/ to purchase a license.", L"TellyMedia - License Required", MB_OK | MB_ICONWARNING);
            return;
        }
        // Limit to 4 slots per bank when licensed
        int usedSlots = 0;
        for (int i = 0; i < SLOTS_PER_BANK; ++i) {
            if (d->grid.banks[bank][i].hasFile) usedSlots++;
        }
        if (usedSlots >= 4 && !d->grid.banks[bank][slot].hasFile) {
            TM_WARN("Slot assignment blocked: limit of 4 slots per bank reached");
            MessageBoxW(d->hWnd, L"License limit: Only 4 slots per bank are available.\n\nUpgrade your license at https://djeventsuite.cloud/ for more slots.", L"TellyMedia - Slot Limit", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    
    SlotData& s = d->grid.banks[bank][slot];
    if (s.hThumb) { DeleteObject(s.hThumb); s.hThumb = nullptr; }

    wcscpy_s(s.filePath, path);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    wcsncpy_s(s.displayName, name, _TRUNCATE);
    const wchar_t* dot = wcsrchr(path, L'.');
    if (dot) { char e[8] = {}; WideCharToMultiByte(CP_ACP, 0, dot + 1, -1, e, 7, 0, 0); strcpy_s(s.fileExt, e); }
    s.hasFile = true;
    s.scaleMode = d->sidebar.scaleMode;
    s.rotation = 0;
    s.builtinFx = FX_NONE;

    int thumbPx = d->metrics.slotSize - (int)(8 * d->dpi);
    if (thumbPx < 16) thumbPx = 16;
    s.hThumb = MakeThumbnail(path, thumbPx);
    TmUI::SaveState(d);
}

static void ClearSlot(TmUIData* d, int bank, int slot)
{
    if (bank < 0 || bank >= NUM_BANKS || slot < 0 || slot >= SLOTS_PER_BANK) return;
    SlotData& s = d->grid.banks[bank][slot];
    if (s.hThumb) { DeleteObject(s.hThumb); s.hThumb = nullptr; }
    ZeroMemory(&s, sizeof(s));
    s.hasFile = false;
    TmUI::SaveState(d);
}

static int BankSlotCount(TmUIData* d, int bank)
{
    int n = 0;
    for (int i = 0; i < SLOTS_PER_BANK; ++i) if (d->grid.banks[bank][i].hasFile) n++;
    return n;
}

// ─── layout ─────────────────────────────────────────────────────────────────
static void ComputeLayout(TmUIData* d)
{
    RECT rc; GetClientRect(d->hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    d->dpi = TmDpi::Scale(d->hWnd);
    d->metrics.Calculate(W, H, d->dpi);
    LayoutMetrics& m = d->metrics;
    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };

    // Tabs in header (after the title)
    int tabW = S(110), tabH = S(34);
    int tabY = (m.headerH - tabH) / 2;
    int tabX = S(220);
    for (int i = 0; i < TAB_COUNT; ++i) {
        d->rcTabs[i] = { tabX + i * (tabW + S(6)), tabY, tabX + i * (tabW + S(6)) + tabW, tabY + tabH };
    }

    // Close button – right side of header
    int closeBtnW = S(44), closeBtnH = tabH;
    d->rcCloseBtn = { W - S(10) - closeBtnW, tabY, W - S(10), tabY + closeBtnH };

    int margin = S(TmSpace::MD);
    int top = m.headerH + S(TmSpace::SM);
    int sidebarW = m.sidebarW;
    int gridLeft = margin;
    int gridRight = W - sidebarW - margin - S(TmSpace::SM);
    int bottom = H - m.statusH - S(TmSpace::SM);

    // Bank bar (2 rows x 7)
    int bankRows = 2, bankCols = 7;
    int bankGap = S(6);
    int bankAreaW = gridRight - gridLeft;
    int bankBtnW = (bankAreaW - bankGap * (bankCols - 1)) / bankCols;
    int bankBtnH = S(30);
    for (int i = 0; i < NUM_BANKS; ++i) {
        int r = i / bankCols, c = i % bankCols;
        int x = gridLeft + c * (bankBtnW + bankGap);
        int y = top + r * (bankBtnH + bankGap);
        d->rcBanks[i] = { x, y, x + bankBtnW, y + bankBtnH };
    }
    int bankBarH = bankRows * bankBtnH + (bankRows - 1) * bankGap;

    // Layout / Options / License tabs: no bank bar, no grid - dedicated pages
    if (d->curTab == TAB_LAYOUT || d->curTab == TAB_OPTIONS || d->curTab == TAB_LICENSE) {
        bankBarH = 0;
        d->rcGrid = { 0, 0, 0, 0 }; // hide grid
    }

    int gridTop = top + bankBarH + S(TmSpace::SM);
    d->rcGrid = { gridLeft, gridTop, gridRight, bottom };
    d->rcSidebar = { gridRight + S(TmSpace::SM), top, gridRight + S(TmSpace::SM) + sidebarW, bottom };

    ZeroMemory(&d->rcLicenseCard, sizeof(d->rcLicenseCard));
    ZeroMemory(&d->rcLicenseBadge, sizeof(d->rcLicenseBadge));
    ZeroMemory(&d->rcLicenseDetails, sizeof(d->rcLicenseDetails));
    ZeroMemory(&d->rcLicenseStatus, sizeof(d->rcLicenseStatus));
    ZeroMemory(&d->rcLicenseLogo, sizeof(d->rcLicenseLogo));
    ZeroMemory(d->rcLicenseButtons, sizeof(d->rcLicenseButtons));
    ZeroMemory(&d->rcLicenseEmail, sizeof(d->rcLicenseEmail));
    ZeroMemory(&d->rcLicensePassword, sizeof(d->rcLicensePassword));
    ZeroMemory(&d->rcLicenseSave, sizeof(d->rcLicenseSave));

    if (d->curTab == TAB_LICENSE) {
        int pad = S(TmSpace::MD);
        int maxW = S(620);
        int cardL = gridLeft + pad;
        int cardR = gridRight - pad;
        if (cardR - cardL > maxW) cardR = cardL + maxW;
        int x = cardL;
        int w = cardR - cardL;
        int logoAreaH = S(180);
        int logoGap = S(12);
        int yLogoTop = gridTop + pad + S(8);
        int badgeSpacing = S(14);
        int y = yLogoTop + logoAreaH + logoGap;

        d->rcLicenseLogo = { x, yLogoTop, x + w, yLogoTop + logoAreaH };

        int labelH = S(18);
        int inputH = S(28);
        int labelGap = S(4);
        int fieldGap = S(8);
        int checkboxH = S(24);
        int loginSpacingAfter = S(24);

        int emailLabelY = y;
        int emailInputY = emailLabelY + labelH + labelGap;
        d->rcLicenseEmail = { x + S(10), emailInputY, x + w - S(10), emailInputY + inputH };
        int passwordLabelY = d->rcLicenseEmail.bottom + fieldGap;
        int passwordInputY = passwordLabelY + labelH + labelGap;
        d->rcLicensePassword = { x + S(10), passwordInputY, x + w - S(10), passwordInputY + inputH };
        int checkboxY = d->rcLicensePassword.bottom + fieldGap;
        d->rcLicenseSave = { x + S(10), checkboxY, x + w - S(10), checkboxY + checkboxH };

        int loginAreaBottom = d->rcLicenseSave.bottom;
        int yBadgeTop = loginAreaBottom + loginSpacingAfter;
        int badgeH = S(28);
        int detailH = S(36);
        int statusH = S(22);
        int bh = S(34);
        int gap = S(8);
        int hdr = S(18);

        int cardTop = yBadgeTop - badgeSpacing;
        d->rcLicenseCard = { cardL, cardTop, cardR, cardTop };
        d->rcLicenseBadge = { x + S(14), yBadgeTop + badgeSpacing, x + S(170), yBadgeTop + badgeSpacing + badgeH };
        d->rcLicenseDetails = { x + S(14), d->rcLicenseBadge.bottom + S(8), cardR - S(14), d->rcLicenseBadge.bottom + S(8) + detailH };
        d->rcLicenseStatus = { x + S(14), d->rcLicenseDetails.bottom + S(6), cardR - S(14), d->rcLicenseDetails.bottom + S(6) + statusH };

        int btnY = d->rcLicenseStatus.bottom + S(14);
        d->rcLicenseButtons[0] = { x, btnY, x + w, btnY + bh };
        btnY += bh + hdr;
        int half = (w - gap) / 2;
        d->rcLicenseButtons[1] = { x, btnY, x + half, btnY + bh };
        d->rcLicenseButtons[2] = { x + half + gap, btnY, x + w, btnY + bh };
        btnY += bh + hdr;
        d->rcLicenseButtons[3] = { x, btnY, x + half, btnY + bh };

        d->rcLicenseCard.bottom = d->rcLicenseButtons[3].bottom + S(14);
    }

    // Layout canvas: a 16:9 output preview centered in the content area (uses the
    // full content height since the Layout tab has no bank bar above it).
    {
        int availL = gridLeft, availT = top, availR = gridRight, availB = bottom;
        int availW = availR - availL, availH = availB - availT;
        int cw = availW, ch = (int)(cw * 9.0f / 16.0f);
        if (ch > availH) { ch = availH; cw = (int)(ch * 16.0f / 9.0f); }
        int cx = availL + (availW - cw) / 2;
        int cy = availT + (availH - ch) / 2;
        d->rcCanvas = { cx, cy, cx + cw, cy + ch };
    }


    // Recompute scroll content height
    int rows = GridRowsTotal();
    d->contentHeight = rows * (m.slotSize + m.slotGap);
    int viewH = d->rcGrid.bottom - d->rcGrid.top;
    int maxScroll = max(0, d->contentHeight - viewH);
    if (d->scrollY > maxScroll) d->scrollY = maxScroll;
    if (d->scrollY < 0) d->scrollY = 0;

    UpdateLicenseControlLayout(d);
}

static void UpdateLicenseControlLayout(TmUIData* d)
{
    if (!d) return;

    auto moveControl = [&](HWND hwnd, const RECT& rc) {
        if (!hwnd) return;
        if (rc.right <= rc.left || rc.bottom <= rc.top) return;
        SetWindowPos(hwnd, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    };

    auto applyFont = [&](HWND hwnd) {
        if (!hwnd) return;
        if (d->fonts.body) SendMessageW(hwnd, WM_SETFONT, (WPARAM)d->fonts.body, TRUE);
    };

    bool show = (d->curTab == TAB_LICENSE);
    moveControl(d->editLicenseEmail, d->rcLicenseEmail);
    moveControl(d->editLicensePassword, d->rcLicensePassword);
    moveControl(d->chkLicenseSave, d->rcLicenseSave);

    applyFont(d->editLicenseEmail);
    applyFont(d->editLicensePassword);
    applyFont(d->chkLicenseSave);

    if (d->editLicenseEmail) ShowWindow(d->editLicenseEmail, show ? SW_SHOW : SW_HIDE);
    if (d->editLicensePassword) ShowWindow(d->editLicensePassword, show ? SW_SHOW : SW_HIDE);
    if (d->chkLicenseSave) ShowWindow(d->chkLicenseSave, show ? SW_SHOW : SW_HIDE);
}

// slot index -> rect in screen/client coords (may be outside viewport)
static RECT SlotRect(TmUIData* d, int idx)
{
    LayoutMetrics& m = d->metrics;
    int cols = m.gridCols;
    int col = idx % cols, row = idx / cols;
    int x = d->rcGrid.left + col * (m.slotSize + m.slotGap);
    int y = d->rcGrid.top + row * (m.slotSize + m.slotGap) - d->scrollY;
    return { x, y, x + m.slotSize, y + m.slotSize };
}

static int SlotAtPoint(TmUIData* d, int px, int py)
{
    if (px < d->rcGrid.left || px > d->rcGrid.right || py < d->rcGrid.top || py > d->rcGrid.bottom)
        return -1;
    LayoutMetrics& m = d->metrics;
    int cols = m.gridCols;
    int relX = px - d->rcGrid.left;
    int relY = py - d->rcGrid.top + d->scrollY;
    int col = relX / (m.slotSize + m.slotGap);
    int row = relY / (m.slotSize + m.slotGap);
    if (col < 0 || col >= cols) return -1;
    int idx = row * cols + col;
    if (idx < 0 || idx >= SLOTS_PER_BANK) return -1;
    // Ensure click is within the slot box (not in the gap)
    RECT r = SlotRect(d, idx);
    if (px >= r.left && px <= r.right && py >= r.top && py <= r.bottom) return idx;
    return -1;
}

static int BankAtPoint(TmUIData* d, int px, int py)
{
    if (!d) return -1;
    for (int i = 0; i < NUM_BANKS; ++i) {
        if (PtInRect(&d->rcBanks[i], { px, py })) return i;
    }
    return -1;
}

// ─── sidebar buttons ──────────────────────────────────────────────────────────
static void BuildSidebar(TmUIData* d, std::vector<SbButton>& out)
{
    out.clear();
    if (d->curTab != TAB_MEDIA) return;
    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };
    RECT sb = d->rcSidebar;
    int pad = S(TmSpace::SM);
    int x = sb.left + pad, w = (sb.right - sb.left) - pad * 2;
    int y = sb.top + pad;
    int bh = S(34), gap = S(8);

    auto card = [&](int rows){ y += S(28); }; // header space; cards drawn in paint

    // Slideshow card
    card(0);
    int half = (w - gap) / 2;
    out.push_back({ { x, y, x + half, y + bh }, SB_SS_START, L"Start", d->sidebar.ssRunning, false });
    out.push_back({ { x + half + gap, y, x + w, y + bh }, SB_SS_STOP, L"Stop", !d->sidebar.ssRunning, false });
    y += bh + gap;
    out.push_back({ { x, y, x + w, y + bh }, SS_ENABLE, L"Enable Slideshow", d->sidebar.ssEnabled, true });
    y += bh + gap;
    // Delay row: [-] value [+]
    int sq = bh;
    out.push_back({ { x, y, x + sq, y + bh }, SB_DELAY_DEC, L"-", false, false });
    out.push_back({ { x + w - sq, y, x + w, y + bh }, SB_DELAY_INC, L"+", false, false });
    y += bh + gap;
    out.push_back({ { x, y, x + w, y + bh }, SB_TRANS, L"", false, false });
    y += bh + gap;
    out.push_back({ { x, y, x + w, y + bh }, SB_DIR, L"", false, false });
    y += bh + gap;
    out.push_back({ { x, y, x + w, y + bh }, SB_LOOP, L"Loop", d->sidebar.ssLoop, true });
    y += bh + gap * 2;

    // Render mode row
    int third = (w - gap * 2) / 3;
    out.push_back({ { x, y, x + third, y + bh }, SB_RENDER_CPU, L"CPU", d->sidebar.renderMode == TM_RENDER_CPU, false });
    out.push_back({ { x + third + gap, y, x + third * 2 + gap, y + bh }, SB_RENDER_GPU, L"GPU", d->sidebar.renderMode == TM_RENDER_GPU, false });
    out.push_back({ { x + third * 2 + gap * 2, y, x + w, y + bh }, SB_RENDER_AUTO, L"Auto", d->sidebar.renderMode == TM_RENDER_AUTO, false });
    y += bh + gap * 2;

    // Scaling card
    card(0);
    out.push_back({ { x, y, x + third, y + bh }, SB_SCALE_ASPECT, L"Aspect", d->sidebar.scaleMode == SCALE_ASPECT, false });
    out.push_back({ { x + third + gap, y, x + third * 2 + gap, y + bh }, SB_SCALE_STRETCH, L"Stretch", d->sidebar.scaleMode == SCALE_STRETCH, false });
    out.push_back({ { x + third * 2 + gap * 2, y, x + w, y + bh }, SB_SCALE_NOSCALE, L"None", d->sidebar.scaleMode == SCALE_NOSCALE, false });
}

static const wchar_t* TransName(int t){ const wchar_t* n[]={L"Transition: Cut",L"Transition: Fade",L"Transition: Crossfade"}; return n[t%3]; }
static const wchar_t* DirName(int t){ const wchar_t* n[]={L"Direction: Forward",L"Direction: Back",L"Direction: Random"}; return n[t%3]; }

// ─── painting ─────────────────────────────────────────────────────────────────
static void LicenseBadgeInfo(TmUIData* d, wchar_t* out, int outMax, COLORREF* bg, COLORREF* fg);
static void LicenseStatusDetails(TmUIData* d, wchar_t* out, int outMax);

static void PaintHeader(TmUIData* d, HDC hdc)
{
    LayoutMetrics& m = d->metrics;
    RECT h = { 0, 0, m.windowW, m.headerH };
    TmDraw::GradientV(hdc, h, RGB(26, 28, 38), RGB(18, 18, 24));
    // accent stripe
    RECT stripe = { 0, m.headerH - (int)(3 * d->dpi), m.windowW, m.headerH };
    TmDraw::GradientV(hdc, stripe, TmColor::ACCENT, TmColor::ACCENT_GLOW);

    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };
    RECT title = { S(16), 0, S(220), m.headerH };
    bool drewLogo = false;
    if (!g_logoImage) {
        if (d && d->hInst) g_logoImage = LoadPngFromResource(d->hInst, 101);
    }
    if (g_logoImage && g_logoImage->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        UINT sw = g_logoImage->GetWidth();
        UINT sh = g_logoImage->GetHeight();
        int availH = m.headerH - S(8);
        if (availH < 8) availH = m.headerH;
        float scale = (float)availH / (float)sh;
        int dw = (int)(sw * scale);
        int dh = (int)(sh * scale);
        int dx = S(16);
        int dy = (m.headerH - dh) / 2;
        g.DrawImage(g_logoImage, dx, dy, dw, dh);
        drewLogo = true;
    }
    if (!drewLogo) {
        TmDraw::TextLeft(hdc, title, L"TellyMedia v4", TmColor::TEXT_PRIMARY, d->fonts.title);
    }

    wchar_t licenseBadge[64] = {};
    COLORREF licenseBg = TmColor::BG_CARD;
    COLORREF licenseFg = TmColor::TEXT_SECOND;
    LicenseBadgeInfo(d, licenseBadge, _countof(licenseBadge), &licenseBg, &licenseFg);

    RECT tabRef = d->rcTabs[0];
    RECT badge = { d->rcCloseBtn.left - S(176), tabRef.top, d->rcCloseBtn.left - S(12), tabRef.bottom };
    if (badge.left > d->rcTabs[TAB_COUNT - 1].right + S(12)) {
        TmDraw::Card(hdc, badge, licenseBg, TmRadius::MD, true);
        TmDraw::TextCenter(hdc, badge, licenseBadge, licenseFg, d->fonts.caption);
    }

    const wchar_t* labels[TAB_COUNT] = { L"Media", L"Layout", L"Options", L"License" };
    for (int i = 0; i < TAB_COUNT; ++i) {
        RECT r = d->rcTabs[i];
        bool active = d->curTab == i;
        bool hover = d->hoverTab == i;
        COLORREF bg = active ? TmColor::ACCENT : (hover ? TmColor::HOVER : TmColor::BG_CARD);
        TmDraw::FillRoundRect(hdc, r, bg, TmRadius::MD);
        TmDraw::TextCenter(hdc, r, labels[i], active ? TmColor::TEXT_PRIMARY : TmColor::TEXT_SECOND, d->fonts.heading);
    }

    // Close button
    {
        RECT r = d->rcCloseBtn;
        COLORREF bg = d->hoverCloseBtn ? RGB(200, 45, 45) : TmColor::BG_CARD;
        COLORREF fg = d->hoverCloseBtn ? RGB(255, 255, 255) : TmColor::TEXT_SECOND;
        TmDraw::FillRoundRect(hdc, r, bg, TmRadius::MD);
        TmDraw::TextCenter(hdc, r, L"✕", fg, d->fonts.heading);
    }
}

static void PaintBanks(TmUIData* d, HDC hdc)
{
    for (int i = 0; i < NUM_BANKS; ++i) {
        RECT r = d->rcBanks[i];
        bool active = d->grid.curBank == i;
        bool hover = d->hoverBank == i;
        COLORREF bankBase = d->grid.bankColors[i];
        COLORREF bg = bankBase;
        if (active) bg = BlendColor(bankBase, RGB(255, 255, 255), 0.18f);
        else if (hover) bg = BlendColor(bankBase, RGB(255, 255, 255), 0.10f);
        TmDraw::FillRoundRect(hdc, r, bg, TmRadius::SM);
        if (active) TmDraw::StrokeRoundRect(hdc, r, TmColor::ACCENT_GLOW, TmRadius::SM, 1);

        wchar_t label[40];
        if (d->grid.bankNames[i][0]) wcscpy_s(label, d->grid.bankNames[i]);
        else swprintf_s(label, L"Bank %d", i + 1);
        COLORREF textColor = IsLightColor(bg) ? RGB(18, 20, 28) : RGB(248, 250, 252);
        TmDraw::TextCenter(hdc, r, label, textColor, d->fonts.caption);

        // slot count badge
        int cnt = BankSlotCount(d, i);
        if (cnt > 0) {
            int bs = (int)(16 * d->dpi);
            RECT badge = { r.right - bs - 2, r.top + 2, r.right - 2, r.top + 2 + bs };
            TmDraw::FillRoundRect(hdc, badge, active ? TmColor::ACCENT_ACTIVE : TmColor::ACCENT, bs / 2);
            wchar_t cb[8]; swprintf_s(cb, L"%d", cnt);
            TmDraw::TextCenter(hdc, badge, cb, TmColor::TEXT_PRIMARY, d->fonts.caption);
        }
    }
}

static void LoadKaraokePrefs(TmUIData* d)
{
    if (!d) return;
    wchar_t path[MAX_PATH];
    KaraokePrefsPath(path, MAX_PATH);
    wchar_t value[16];
    GetPrivateProfileStringW(L"Karaoke", L"AutoHide", L"1", value, _countof(value), path);
    d->karaokeAutoHide = (value[0] == L'1' || _wcsicmp(value, L"true") == 0 || _wcsicmp(value, L"yes") == 0 || _wcsicmp(value, L"on") == 0);
}

static void PaintGrid(TmUIData* d, HDC hdc)
{
    LayoutMetrics& m = d->metrics;
    // panel background
    RECT panel = d->rcGrid; InflateRect(&panel, (int)(6*d->dpi), (int)(6*d->dpi));
    TmDraw::FillRoundRect(hdc, panel, TmColor::BG_PANEL, TmRadius::MD);

    HRGN clip = CreateRectRgn(d->rcGrid.left, d->rcGrid.top, d->rcGrid.right, d->rcGrid.bottom);
    SelectClipRgn(hdc, clip);

    int bank = d->grid.curBank;
    
    // Check license status for slot limit indicator
    LicenseState st = {};
    TmLicense::Init(&st);
    bool isLicensed = (d && d->host && d->host->HostGetLicenseState(&st) && st.status == LIC_VALID);
    
    for (int i = 0; i < SLOTS_PER_BANK; ++i) {
        RECT r = SlotRect(d, i);
        if (r.bottom < d->rcGrid.top || r.top > d->rcGrid.bottom) continue;
        if (r.right > d->rcGrid.right) continue;

        SlotData& s = d->grid.banks[bank][i];
        bool selected = (d->grid.selectedSlot == i);
        bool hover = (d->hoverSlot == i);
        
        // Check if this slot is beyond the 4-slot limit
        bool isLocked = (isLicensed && i >= 4);
        
        COLORREF bg = selected ? TmColor::BG_ELEVATED : (hover ? TmColor::HOVER : (isLocked ? RGB(40, 40, 50) : TmColor::BG_CARD));
        TmDraw::FillRoundRect(hdc, r, bg, TmRadius::MD);

        if (s.hasFile && s.hThumb) {
            int pad = (int)(4 * d->dpi);
            RECT ir = { r.left + pad, r.top + pad, r.right - pad, r.bottom - pad };
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP ob = (HBITMAP)SelectObject(mem, s.hThumb);
            BITMAP bm; GetObject(s.hThumb, sizeof(bm), &bm);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, ir.left, ir.top, ir.right - ir.left, ir.bottom - ir.top,
                       mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
            SelectObject(mem, ob); DeleteDC(mem);

            // name strip
            RECT ns = { r.left, r.bottom - (int)(18*d->dpi), r.right, r.bottom };
            TmDraw::FillRoundRect(hdc, ns, RGB(0,0,0), 0);
            RECT nt = ns; nt.left += (int)(4*d->dpi);
            TmDraw::TextLeft(hdc, nt, s.displayName, TmColor::TEXT_SECOND, d->fonts.caption);
        } else {
            if (isLocked) {
                // Show lock icon for slots beyond limit
                RECT t = r;
                TmDraw::TextCenter(hdc, t, L"🔒", RGB(150, 150, 150), d->fonts.caption);
            } else {
                wchar_t num[12]; swprintf_s(num, L"%d", i + 1);
                COLORREF nc = TmColor::TEXT_DISABLED;
                RECT t = r;
                TmDraw::TextCenter(hdc, t, hover ? L"Click / Drop" : num, hover ? TmColor::TEXT_THIRD : nc, d->fonts.caption);
            }
        }

        if (d->slotMovePending && d->slotMoveSource == i) {
            TmDraw::StrokeRoundRect(hdc, r, RGB(255, 200, 80), TmRadius::MD, 3);
        } else if (selected) {
            TmDraw::StrokeRoundRect(hdc, r, TmColor::ACCENT, TmRadius::MD, 2);
        } else if (isLocked) {
            // Draw subtle border for locked slots
            TmDraw::StrokeRoundRect(hdc, r, RGB(80, 80, 90), TmRadius::MD, 1);
        }
    }

    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);

    // Draw license overlay if not licensed
    if (d && d->host && d->host->HostGetLicenseState(&st) && st.status != LIC_VALID) {
        RECT overlay = d->rcGrid;
        HRGN overlayClip = CreateRectRgn(overlay.left, overlay.top, overlay.right, overlay.bottom);
        SelectClipRgn(hdc, overlayClip);
        
        // Semi-transparent dark overlay
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 128, AC_SRC_ALPHA };
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, overlay.right - overlay.left, overlay.bottom - overlay.top);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        
        RECT fillRect = { 0, 0, overlay.right - overlay.left, overlay.bottom - overlay.top };
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &fillRect, brush);
        DeleteObject(brush);
        
        AlphaBlend(hdc, overlay.left, overlay.top, overlay.right - overlay.left, overlay.bottom - overlay.top,
                   memDC, 0, 0, overlay.right - overlay.left, overlay.bottom - overlay.top, bf);
        
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        
        // Draw "Activate License" message
        RECT textRect = overlay;
        int textH = (int)(60 * d->dpi);
        textRect.top = (overlay.top + overlay.bottom - textH) / 2;
        textRect.bottom = textRect.top + textH;
        
        const wchar_t* msg = L"ACTIVATE LICENSE TO USE SLOTS";
        TmDraw::TextCenter(hdc, textRect, msg, RGB(255, 255, 255), d->fonts.body);
        
        RECT subRect = textRect;
        subRect.top = textRect.bottom + (int)(10 * d->dpi);
        subRect.bottom = subRect.top + (int)(30 * d->dpi);
        const wchar_t* subMsg = L"Visit https://djeventsuite.cloud/";
        TmDraw::TextCenter(hdc, subRect, subMsg, RGB(200, 200, 200), d->fonts.caption);
        
        SelectClipRgn(hdc, nullptr);
        DeleteObject(overlayClip);
    }

    // scrollbar
    int viewH = d->rcGrid.bottom - d->rcGrid.top;
    if (d->contentHeight > viewH) {
        int sbw = ScrollBarW(d);
        RECT track = { d->rcGrid.right - sbw, d->rcGrid.top, d->rcGrid.right, d->rcGrid.bottom };
        TmDraw::FillRoundRect(hdc, track, TmColor::BG_DEEP, sbw / 2);
        int thumbH = max((int)(30*d->dpi), viewH * viewH / d->contentHeight);
        int maxScroll = d->contentHeight - viewH;
        int thumbY = track.top + (maxScroll > 0 ? d->scrollY * (viewH - thumbH) / maxScroll : 0);
        RECT thumb = { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
        TmDraw::FillRoundRect(hdc, thumb, TmColor::ACCENT, (sbw - 4) / 2);
    }
}

static void PaintSidebar(TmUIData* d, HDC hdc)
{
    RECT sb = d->rcSidebar;
    TmDraw::FillRoundRect(hdc, sb, TmColor::BG_PANEL, TmRadius::MD);

    // Options tab: placeholder
    if (d->curTab == TAB_OPTIONS) {
        RECT t = sb; t.top += (int)(20*d->dpi);
        TmDraw::TextCenter(hdc, t, L"Options (Phase 6)", TmColor::TEXT_THIRD, d->fonts.body);
        return;
    }

    // Layout tab sidebar is handled by child window
    if (d->curTab == TAB_LAYOUT) return;

    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };
    std::vector<SbButton> btns;
    BuildSidebar(d, btns);

    // Card headers (positions mirror BuildSidebar's y advances)
    for (auto& b : btns) if (b.id == SB_SS_START) {
        RECT h1 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
        TmDraw::TextLeft(hdc, h1, L"SLIDESHOW", TmColor::TEXT_THIRD, d->fonts.caption);
    }
    for (auto& b : btns) if (b.id == SB_SCALE_ASPECT) {
        RECT h2 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
        TmDraw::TextLeft(hdc, h2, L"SCALING", TmColor::TEXT_THIRD, d->fonts.caption);
    }
    for (auto& b : btns) if (b.id == SB_RENDER_AUTO) {
        RECT h3 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
        TmDraw::TextLeft(hdc, h3, L"RENDER", TmColor::TEXT_THIRD, d->fonts.caption);
    }

    for (auto& b : btns) {
        bool hover = d->hoverSidebar == b.id;
        COLORREF bg = b.active ? TmColor::ACCENT : (hover ? TmColor::HOVER : TmColor::BG_CARD);
        TmDraw::FillRoundRect(hdc, b.rc, bg, TmRadius::SM);

        wchar_t buf[64];
        const wchar_t* label = b.label;
        if (b.id == SB_TRANS) { wcscpy_s(buf, TransName(d->sidebar.ssTransition)); label = buf; }
        else if (b.id == SB_DIR) { wcscpy_s(buf, DirName(d->sidebar.ssDirection)); label = buf; }
        else if (b.id == SB_LOOP) { swprintf_s(buf, L"Loop: %s", d->sidebar.ssLoop ? L"On" : L"Off"); label = buf; }
        else if (b.id == SS_ENABLE) { label = d->sidebar.ssEnabled ? L"Disable Slideshow" : L"Enable Slideshow"; }
        else if (b.id == SB_RENDER_CPU) { label = L"CPU"; }
        else if (b.id == SB_RENDER_GPU) { label = L"GPU"; }
        else if (b.id == SB_RENDER_AUTO) { label = L"Auto"; }
        TmDraw::TextCenter(hdc, b.rc, label, b.active ? TmColor::TEXT_PRIMARY : TmColor::TEXT_SECOND, d->fonts.body);
    }

    // Delay value centered between dec/inc
    for (auto& b : btns) if (b.id == SB_DELAY_DEC) {
        RECT dv = { b.rc.right, b.rc.top, 0, b.rc.bottom };
        for (auto& e : btns) if (e.id == SB_DELAY_INC) dv.right = e.rc.left;
        wchar_t v[32]; swprintf_s(v, L"Delay %ds", d->sidebar.ssDelaySec);
        TmDraw::TextCenter(hdc, dv, v, TmColor::TEXT_PRIMARY, d->fonts.body);
    }
}

static void PaintStatus(TmUIData* d, HDC hdc)
{
    LayoutMetrics& m = d->metrics;
    RECT s = { 0, m.windowH - m.statusH, m.windowW, m.windowH };
    TmDraw::FillRoundRect(hdc, s, TmColor::BG_DEEP, 0);

    wchar_t licenseBadge[64] = {};
    COLORREF licenseBg = TmColor::BG_CARD;
    COLORREF licenseFg = TmColor::TEXT_SECOND;
    LicenseBadgeInfo(d, licenseBadge, _countof(licenseBadge), &licenseBg, &licenseFg);

    RECT badge = { s.right - (int)(190 * d->dpi), s.top + (int)(3 * d->dpi), s.right - (int)(8 * d->dpi), s.bottom - (int)(3 * d->dpi) };
    TmDraw::Card(hdc, badge, licenseBg, TmRadius::MD, false);
    TmDraw::TextCenter(hdc, badge, licenseBadge, licenseFg, d->fonts.caption);

    wchar_t renderText[64] = L"Render: N/A";
    if (d && d->host) d->host->HostGetRenderModeText(renderText, _countof(renderText));
    else {
        switch (d->sidebar.renderMode) {
        case TM_RENDER_CPU:  wcscpy_s(renderText, L"Render: CPU"); break;
        case TM_RENDER_GPU:  wcscpy_s(renderText, L"Render: GPU"); break;
        default:             wcscpy_s(renderText, L"Render: Auto"); break;
        }
    }
    wchar_t txt[240];
    int bank = d->grid.curBank;
    swprintf_s(txt, L"  Bank %d  |  %d media  |  Slot %s  |  %d%% DPI  |  %s",
               bank + 1, BankSlotCount(d, bank),
               d->grid.selectedSlot >= 0 ? L"selected" : L"none",
               (int)(d->dpi * 100),
               renderText);
    RECT t = s; t.left += (int)(8*d->dpi);
    t.right = badge.left - (int)(10 * d->dpi);
    TmDraw::TextLeft(hdc, t, txt, TmColor::TEXT_THIRD, d->fonts.caption);
}

static void OnPaint(TmUIData* d)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(d->hWnd, &ps);
    RECT rc; GetClientRect(d->hWnd, &rc);

    // double buffer
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(TmColor::BG_BASE);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    PaintHeader(d, mem);

    // Per-tab content
    if (d->curTab == TAB_MEDIA) {
        PaintBanks(d, mem);
        PaintGrid(d, mem);
        PaintSidebar(d, mem);
    } else if (d->curTab == TAB_LAYOUT) {
        PaintLayoutCanvas(d, mem);

        // Draw shader switcher row under the canvas for the selected shader panel
        {
            std::vector<SbButton> shaderBtns;
            BuildLayoutShaderRowUnderCanvas(d, shaderBtns);
            for (auto& b : shaderBtns) {
                bool hover = (d->hoverSidebar == b.id);
                COLORREF bg = hover ? TmColor::HOVER : TmColor::BG_CARD;
                TmDraw::FillRoundRect(mem, b.rc, bg, TmRadius::SM);
                TmDraw::TextCenter(mem, b.rc, b.label, TmColor::TEXT_SECOND, d->fonts.body);
            }
        }
        // Layout sidebar with both panel list scroll (5 items) and full sidebar scroll
        auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };
        RECT sb = d->rcSidebar;
        TmDraw::FillRoundRect(mem, sb, TmColor::BG_PANEL, TmRadius::MD);
        std::vector<SbButton> btns;
        BuildLayoutSidebar(d, btns);

        // Clip to sidebar area for full sidebar scrolling
        HRGN clip = CreateRectRgn(sb.left, sb.top, sb.right, sb.bottom);
        SelectClipRgn(mem, clip);

        // Offset all buttons by sidebar scroll position
        for (auto& b : btns) {
            OffsetRect(&b.rc, 0, -d->sidebarScrollY);
        }

        // Panel list header (offset by sidebar scroll)
        RECT h0 = { sb.left + S(12), d->rcPanelList.top - S(24) - d->sidebarScrollY, sb.right - S(12), d->rcPanelList.top - S(4) - d->sidebarScrollY };
        TmDraw::TextLeft(mem, h0, L"PANELS  (click to select)", TmColor::TEXT_THIRD, d->fonts.caption);

        // Clip to panel list area for panel list scrolling
        HRGN panelClip = CreateRectRgn(d->rcPanelList.left, d->rcPanelList.top - d->sidebarScrollY, d->rcPanelList.right, d->rcPanelList.bottom - d->sidebarScrollY);
        SelectClipRgn(mem, panelClip);
        DeleteObject(clip);

        // Offset panel list buttons by panel list scroll (in addition to sidebar scroll)
        for (auto& b : btns) {
            bool isPanelItem = (b.id >= LB_PANEL_SELECT_BASE && b.id < LB_PANEL_SELECT_BASE + MAX_OVERLAY_PANELS);
            if (isPanelItem) {
                OffsetRect(&b.rc, 0, -d->panelListScrollY);
            }
        }

        // Draw panel list items
        for (auto& b : btns) {
            bool isPanelItem = (b.id >= LB_PANEL_SELECT_BASE && b.id < LB_PANEL_SELECT_BASE + MAX_OVERLAY_PANELS);
            if (!isPanelItem) continue;

            bool hover = d->hoverSidebar == b.id;
            COLORREF bg, fg;
            if (b.active) {
                bg = TmColor::ACCENT;
                fg = TmColor::TEXT_PRIMARY;
            } else if (hover) {
                bg = TmColor::HOVER;
                fg = TmColor::TEXT_SECOND;
            } else {
                bg = TmColor::BG_ELEVATED;
                fg = TmColor::TEXT_SECOND;
            }
            TmDraw::FillRoundRect(mem, b.rc, bg, TmRadius::SM);
            RECT lr = b.rc; lr.left += S(8);
            TmDraw::TextLeft(mem, lr, b.label, fg, d->fonts.body);
        }

        // Restore to sidebar clip
        SelectClipRgn(mem, CreateRectRgn(sb.left, sb.top, sb.right, sb.bottom));
        DeleteObject(panelClip);

        // Draw scrollbar for panel list if needed
        int panelListContentH = d->numLayoutPanels * (S(32) + S(8));
        int panelListViewH = d->rcPanelList.bottom - d->rcPanelList.top;
        if (panelListContentH > panelListViewH) {
            int sbw = ScrollBarW(d);
            RECT track = { d->rcPanelList.right - sbw, d->rcPanelList.top - d->sidebarScrollY, d->rcPanelList.right, d->rcPanelList.bottom - d->sidebarScrollY };
            TmDraw::FillRoundRect(mem, track, TmColor::BG_DEEP, sbw / 2);
            int thumbH = max((int)(30*d->dpi), panelListViewH * panelListViewH / panelListContentH);
            int maxScroll = panelListContentH - panelListViewH;
            int thumbY = track.top + (maxScroll > 0 ? d->panelListScrollY * (panelListViewH - thumbH) / maxScroll : 0);
            RECT thumb = { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
            TmDraw::FillRoundRect(mem, thumb, TmColor::ACCENT, (sbw - 4) / 2);
        }

        // Draw the rest of the sidebar (offset by sidebar scroll only)
        for (auto& b : btns) {
            bool isPanelItem = (b.id >= LB_PANEL_SELECT_BASE && b.id < LB_PANEL_SELECT_BASE + MAX_OVERLAY_PANELS);
            if (isPanelItem) continue;

            // Card headers
            if (b.id == LB_ADD_MAIN) {
                RECT h1 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
                TmDraw::TextLeft(mem, h1, L"ADD PANEL", TmColor::TEXT_THIRD, d->fonts.caption);
            }
            if (b.id == LB_POS_TL) {
                RECT h2 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
                TmDraw::TextLeft(mem, h2, L"POSITION", TmColor::TEXT_THIRD, d->fonts.caption);
            }
            if (b.id == LB_Z_UP) {
                RECT h3 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
                TmDraw::TextLeft(mem, h3, L"ARRANGE", TmColor::TEXT_THIRD, d->fonts.caption);
            }
            if (b.id == LB_LOAD_IMAGE || b.id == LB_EDIT_TEXT || b.id == LB_TEXT_FONT ||
                b.id == LB_TEXT_SIZE_DEC || b.id == LB_TEXT_SIZE_INC ||
                b.id == LB_TEXT_COLOR || b.id == LB_BG_COLOR) {
                RECT h4 = { sb.left + S(12), b.rc.top - S(24), sb.right - S(12), b.rc.top - S(4) };
                TmDraw::TextLeft(mem, h4, L"PROPERTIES", TmColor::TEXT_THIRD, d->fonts.caption);
            }

            // Draw button
            bool hover = d->hoverSidebar == b.id;
            COLORREF bg = b.active ? TmColor::ACCENT : (hover ? TmColor::HOVER : TmColor::BG_CARD);
            TmDraw::FillRoundRect(mem, b.rc, bg, TmRadius::SM);
            TmDraw::TextCenter(mem, b.rc, b.label, b.active ? TmColor::TEXT_PRIMARY : TmColor::TEXT_SECOND, d->fonts.body);
        }

        // Restore clip
        SelectClipRgn(mem, nullptr);
        DeleteObject(clip);

        // Draw scrollbar for full sidebar if needed
        int viewH = sb.bottom - sb.top;
        if (d->sidebarContentHeight > viewH) {
            int sbw = ScrollBarW(d);
            RECT track = { sb.right - sbw, sb.top, sb.right, sb.bottom };
            TmDraw::FillRoundRect(mem, track, TmColor::BG_DEEP, sbw / 2);
            int thumbH = max((int)(30*d->dpi), viewH * viewH / d->sidebarContentHeight);
            int maxScroll = d->sidebarContentHeight - viewH;
            int thumbY = track.top + (maxScroll > 0 ? d->sidebarScrollY * (viewH - thumbH) / maxScroll : 0);
            RECT thumb = { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
            TmDraw::FillRoundRect(mem, thumb, TmColor::ACCENT, (sbw - 4) / 2);
        }
    } else if (d->curTab == TAB_OPTIONS) {
        PaintOptionsPage(d, mem);
    } else if (d->curTab == TAB_LICENSE) {
        PaintLicensePage(d, mem);
    }
    PaintStatus(d, mem);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(d->hWnd, &ps);
}

// ─── interaction ──────────────────────────────────────────────────────────────
static void OpenFileForSlot(TmUIData* d, int slot)
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = d->hWnd;
    ofn.lpstrFilter = L"Media\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.mp4;*.avi;*.mov;*.mkv;*.wmv;*.webm\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        AssignSlot(d, d->grid.curBank, slot, file);
        d->grid.selectedSlot = slot;
        InvalidateRect(d->hWnd, nullptr, FALSE);
    }
}

static void StopSlideshow(TmUIData* d);

static void ActivateSlot(TmUIData* d, int bank, int slot)
{
    if (!d || bank < 0 || bank >= NUM_BANKS || slot < 0 || slot >= SLOTS_PER_BANK) return;
    d->grid.curBank = bank;
    d->grid.selectedSlot = slot;
    SlotData& s = d->grid.banks[bank][slot];
    if (!s.hasFile) {
        OpenFileForSlot(d, slot);
        return;
    }

    // If the slideshow is running, stop it so this slot stays visible until the
    // user restarts the show explicitly.
    if (d->sidebar.ssRunning) {
        StopSlideshow(d);
    }

    if (d->host) d->host->HostTransitionMode(d->sidebar.ssTransition);
    if (d->host) d->host->HostPlayMedia(s.filePath, s.scaleMode ? s.scaleMode : d->sidebar.scaleMode);
}

static void CancelSlotInteraction(TmUIData* d)
{
    if (!d) return;
    if (d->hWnd) KillTimer(d->hWnd, 3);
    if (GetCapture() == d->hWnd) ReleaseCapture();
    d->slotClickPending = false;
    d->slotMovePending = false;
    d->slotClickBank = -1;
    d->slotClickSlot = -1;
    d->slotClickStart = { 0, 0 };
    d->slotMoveSourceBank = -1;
    d->slotMoveSource = -1;
}

static void CancelSlotMove(TmUIData* d)
{
    CancelSlotInteraction(d);
}

static void BeginSlotMove(TmUIData* d, int bank, int slot)
{
    if (!d || bank < 0 || bank >= NUM_BANKS || slot < 0 || slot >= SLOTS_PER_BANK) return;
    SlotData& s = d->grid.banks[bank][slot];
    if (!s.hasFile) {
        TM_INFO("Cannot move an empty slot");
        return;
    }
    d->slotClickPending = false;
    d->slotMovePending = true;
    d->slotMoveSourceBank = bank;
    d->slotMoveSource = slot;
    d->grid.curBank = bank;
    d->grid.selectedSlot = slot;
    TM_INFO("Move slot armed: bank=%d slot=%d", bank, slot);
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

static void CommitSlotMove(TmUIData* d, int dstBank, int dstSlot)
{
    if (!d || !d->slotMovePending || d->slotMoveSourceBank < 0 || d->slotMoveSourceBank >= NUM_BANKS) return;
    if (dstBank < 0 || dstBank >= NUM_BANKS || dstSlot < 0 || dstSlot >= SLOTS_PER_BANK) return;
    if (dstSlot < 0 || dstSlot >= SLOTS_PER_BANK) return;

    int srcBank = d->slotMoveSourceBank;
    int srcSlot = d->slotMoveSource;
    if (srcBank == dstBank && srcSlot == dstSlot) {
        CancelSlotMove(d);
        InvalidateRect(d->hWnd, nullptr, FALSE);
        return;
    }

    SlotData temp = d->grid.banks[srcBank][srcSlot];
    d->grid.banks[srcBank][srcSlot] = d->grid.banks[dstBank][dstSlot];
    d->grid.banks[dstBank][dstSlot] = temp;

    d->grid.curBank = dstBank;
    d->grid.selectedSlot = dstSlot;
    CancelSlotMove(d);
    TM_INFO("Moved/swapped slot %d:%d -> %d:%d", srcBank, srcSlot, dstBank, dstSlot);
    TmUI::SaveState(d);
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

static void ShowSlotContextMenu(TmUIData* d, int slot, POINT ptClient)
{
    if (!d || !d->hWnd || slot < 0 || slot >= SLOTS_PER_BANK) return;

    SlotData& s = d->grid.banks[d->grid.curBank][slot];
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    enum { CM_SLOT_LOAD = 1, CM_SLOT_CLEAR = 2, CM_SLOT_MOVE = 3 };
    AppendMenuW(menu, MF_STRING, CM_SLOT_LOAD, s.hasFile ? L"Replace Image..." : L"Load Image...");
    AppendMenuW(menu, MF_STRING | (s.hasFile ? 0 : MF_GRAYED), CM_SLOT_CLEAR, L"Clear Slot");
    AppendMenuW(menu, MF_STRING | (s.hasFile ? 0 : MF_GRAYED), CM_SLOT_MOVE, L"Move / Swap Slot...");

    ClientToScreen(d->hWnd, &ptClient);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                             ptClient.x, ptClient.y, 0, d->hWnd, nullptr);
    DestroyMenu(menu);

    if (cmd == CM_SLOT_LOAD) {
        OpenFileForSlot(d, slot);
    } else if (cmd == CM_SLOT_CLEAR && s.hasFile) {
        ClearSlot(d, d->grid.curBank, slot);
        InvalidateRect(d->hWnd, nullptr, FALSE);
    } else if (cmd == CM_SLOT_MOVE && s.hasFile) {
        BeginSlotMove(d, d->grid.curBank, slot);
    }
}

static void StartSlideshow(TmUIData* d)
{
    if (!d) return;
    d->sidebar.ssEnabled = true;
    d->sidebar.ssRunning = true;
    KillTimer(d->hWnd, 1);
    SetTimer(d->hWnd, 1, d->sidebar.ssDelaySec * 1000, nullptr);
    if (d->host) d->host->HostSlideshowStart();
    RedrawWindow(d->hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static void StopSlideshow(TmUIData* d)
{
    if (!d) return;
    d->sidebar.ssRunning = false;
    KillTimer(d->hWnd, 1);
    if (d->host) d->host->HostSlideshowStop();
    RedrawWindow(d->hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static void HandleSidebarClick(TmUIData* d, int id)
{
    TM_INFO("HandleSidebarClick: id=%d", id);
    switch (id) {
    case SS_ENABLE:
        d->sidebar.ssEnabled = !d->sidebar.ssEnabled;
        TM_INFO("Slideshow enabled=%d", d->sidebar.ssEnabled);
        if (!d->sidebar.ssEnabled) {
            // Always stop any active slideshow/media immediately when disabling,
            // even if the local running flag has drifted out of sync.
            StopSlideshow(d);
        }
        break;
    case SB_SS_START:
        // Starting slideshow should also put the UI back into the enabled state
        // so the toggle reflects that playback is active again.
        StartSlideshow(d);
        TM_INFO("Slideshow started, delay=%d", d->sidebar.ssDelaySec);
        break;
    case SB_SS_STOP:  StopSlideshow(d); TM_INFO("Slideshow stopped"); break;
    case SB_DELAY_DEC: if (d->sidebar.ssDelaySec > 1) { d->sidebar.ssDelaySec--; if (d->sidebar.ssRunning) { KillTimer(d->hWnd, 1); SetTimer(d->hWnd, 1, d->sidebar.ssDelaySec * 1000, nullptr); } TM_INFO("Delay decreased to %d", d->sidebar.ssDelaySec); } break;
    case SB_DELAY_INC: if (d->sidebar.ssDelaySec < 60) { d->sidebar.ssDelaySec++; if (d->sidebar.ssRunning) { KillTimer(d->hWnd, 1); SetTimer(d->hWnd, 1, d->sidebar.ssDelaySec * 1000, nullptr); } TM_INFO("Delay increased to %d", d->sidebar.ssDelaySec); } break;
    case SB_TRANS: d->sidebar.ssTransition = (d->sidebar.ssTransition + 1) % 3; if (d->host) d->host->HostTransitionMode(d->sidebar.ssTransition); TM_INFO("Transition mode=%d", d->sidebar.ssTransition); break;
    case SB_DIR:   d->sidebar.ssDirection = (d->sidebar.ssDirection + 1) % 3; TM_INFO("Direction=%d", d->sidebar.ssDirection); break;
    case SB_LOOP:  d->sidebar.ssLoop = !d->sidebar.ssLoop; TM_INFO("Loop=%d", d->sidebar.ssLoop); break;
    case SB_RENDER_CPU:  d->sidebar.renderMode = TM_RENDER_CPU;  if (d->host) d->host->HostRenderMode(d->sidebar.renderMode); TM_INFO("Render=CPU"); break;
    case SB_RENDER_GPU:  d->sidebar.renderMode = TM_RENDER_GPU;  if (d->host) d->host->HostRenderMode(d->sidebar.renderMode); TM_INFO("Render=GPU"); break;
    case SB_RENDER_AUTO: d->sidebar.renderMode = TM_RENDER_AUTO; if (d->host) d->host->HostRenderMode(d->sidebar.renderMode); TM_INFO("Render=Auto"); break;
    case SB_SCALE_ASPECT:  d->sidebar.scaleMode = SCALE_ASPECT;  if (d->host) d->host->HostScaleMode(SCALE_ASPECT); TM_INFO("Scale=Aspect"); break;
    case SB_SCALE_STRETCH: d->sidebar.scaleMode = SCALE_STRETCH; if (d->host) d->host->HostScaleMode(SCALE_STRETCH); TM_INFO("Scale=Stretch"); break;
    case SB_SCALE_NOSCALE: d->sidebar.scaleMode = SCALE_NOSCALE; if (d->host) d->host->HostScaleMode(SCALE_NOSCALE); TM_INFO("Scale=None"); break;
    }
    RedrawWindow(d->hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static void DoSlideshowStep(TmUIData* d)
{
    if (!d->sidebar.ssEnabled) {
        TM_INFO("SlideshowStep: slideshow is disabled, skipping");
        return;
    }
    int bank = d->grid.curBank;
    // collect filled slots
    std::vector<int> filled;
    for (int i = 0; i < SLOTS_PER_BANK; ++i) if (d->grid.banks[bank][i].hasFile) filled.push_back(i);
    if (filled.empty()) {
        TM_INFO("SlideshowStep: no filled slots in bank %d", bank);
        return;
    }
    int cur = d->grid.selectedSlot;
    int pos = 0;
    for (size_t i = 0; i < filled.size(); ++i) if (filled[i] == cur) { pos = (int)i; break; }
    if (d->sidebar.ssDirection == 0) pos = (pos + 1) % (int)filled.size();
    else if (d->sidebar.ssDirection == 1) pos = (pos - 1 + (int)filled.size()) % (int)filled.size();
    else pos = rand() % (int)filled.size();
    d->grid.selectedSlot = filled[pos];
    TM_INFO("SlideshowStep: bank=%d slot=%d direction=%d", bank, filled[pos], d->sidebar.ssDirection);
    if (d->host) {
        SlotData& s = d->grid.banks[bank][filled[pos]];
        if (s.hasFile) {
            TM_INFO("SlideshowStep: playing %S", s.filePath);
            d->host->HostTransitionMode(d->sidebar.ssTransition);
            d->host->HostPlayMedia(s.filePath, s.scaleMode ? s.scaleMode : d->sidebar.scaleMode);
        }
    }
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

// ─── Layout panel helpers ─────────────────────────────────────────────────────
static const wchar_t* PanelTypeName(int t){ const wchar_t* n[]={L"Main",L"Image",L"Text",L"Shader",L"Now Playing"}; return n[(t>=0&&t<5)?t:0]; }

static void ClearPanelImageCache(TmUIData* d, int idx)
{
    if (idx >= 0 && idx < MAX_OVERLAY_PANELS && d->panelImageCache[idx]) {
        delete d->panelImageCache[idx];
        d->panelImageCache[idx] = nullptr;
    }
}

static void GetNowPlayingText(TmUIData* d, wchar_t* outText, int outTextMax)
{
    if (!outText || outTextMax <= 0) return;
    outText[0] = L'\0';
    if (d && d->host) d->host->HostGetNowPlayingText(outText, outTextMax);
}

static void GetRenderModeText(TmUIData* d, wchar_t* outText, int outTextMax)
{
    if (!outText || outTextMax <= 0) return;
    outText[0] = L'\0';
    if (d && d->host) d->host->HostGetRenderModeText(outText, outTextMax);
}

static RECT PanelRectPx(TmUIData* d, const OverlayPanel& p)
{
    int cw = d->rcCanvas.right - d->rcCanvas.left;
    int ch = d->rcCanvas.bottom - d->rcCanvas.top;
    RECT r;
    r.left   = d->rcCanvas.left + (int)(p.x * cw + 0.5f);
    r.top    = d->rcCanvas.top  + (int)(p.y * ch + 0.5f);
    r.right  = d->rcCanvas.left + (int)((p.x + p.w) * cw + 0.5f);
    r.bottom = d->rcCanvas.top  + (int)((p.y + p.h) * ch + 0.5f);
    return r;
}

// Dot size (drawn) and hit size (clickable) for corner handles
static const int kCornerDot = 10;  // px drawn
static const int kCornerHit = 14;  // px hit area (larger for easier grabbing)

// Returns the DRAW rect for one of the 4 corner handles (0=TL,1=TR,2=BL,3=BR)
static RECT PanelCornerDraw(TmUIData* d, const OverlayPanel& p, int corner)
{
    RECT r = PanelRectPx(d, p);
    int ds = (int)(kCornerDot * d->dpi);
    int cx = (corner == 0 || corner == 2) ? r.left : r.right;
    int cy = (corner == 0 || corner == 1) ? r.top  : r.bottom;
    return { cx - ds/2, cy - ds/2, cx + ds/2, cy + ds/2 };
}
// Returns the HIT rect for one of the 4 corner handles (0=TL,1=TR,2=BL,3=BR)
static RECT PanelCornerHandle(TmUIData* d, const OverlayPanel& p, int corner)
{
    RECT r = PanelRectPx(d, p);
    int hs = (int)(kCornerHit * d->dpi);
    int cx = (corner == 0 || corner == 2) ? r.left : r.right;
    int cy = (corner == 0 || corner == 1) ? r.top  : r.bottom;
    return { cx - hs/2, cy - hs/2, cx + hs/2, cy + hs/2 };
}
// Returns which corner handle (0=TL,1=TR,2=BL,3=BR) is hit, or -1
static int PanelCornerHit(TmUIData* d, const OverlayPanel& p, int px, int py)
{
    for (int c = 0; c < 4; ++c) {
        RECT hr = PanelCornerHandle(d, p, c);
        if (PtInRect(&hr, { px, py })) return c;
    }
    return -1;
}
static RECT PanelHandlePx(TmUIData* d, const OverlayPanel& p)
{
    return PanelCornerHandle(d, p, 3); // BR for legacy callers
}

static int PanelAtPoint(TmUIData* d, int px, int py)
{
    int best = -1, bestZ = -1;
    for (int i = 0; i < d->numLayoutPanels; ++i) {
        RECT r = PanelRectPx(d, d->layoutPanels[i]);
        if (px >= r.left && px <= r.right && py >= r.top && py <= r.bottom) {
            if (d->layoutPanels[i].zOrder >= bestZ) { bestZ = d->layoutPanels[i].zOrder; best = i; }
        }
    }
    return best;
}

static int PanelAtPointForContext(TmUIData* d, int px, int py)
{
    if (!d->directSelectEnabled && d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
        RECT selectedRect = PanelRectPx(d, d->layoutPanels[d->selectedPanel]);
        if (px >= selectedRect.left && px <= selectedRect.right &&
            py >= selectedRect.top && py <= selectedRect.bottom) {
            return d->selectedPanel;
        }
    }
    return PanelAtPoint(d, px, py);
}

static int GetShaderCountAndNames(TmUIData* d, wchar_t* outNames, int outNamesMax)
{
    if (!outNames || outNamesMax <= 0) return 0;
    outNames[0] = L'\0';
    if (!d || !d->host) return 0;
    d->host->HostGetShaderNames(outNames, outNamesMax);
    if (!outNames[0]) return 0;

    int count = 1;
    for (const wchar_t* p = outNames; *p; ++p) {
        if (*p == L'|') count++;
    }
    return count;
}

static std::wstring ShaderDisplayNameFromRaw(const wchar_t* rawName)
{
    std::wstring name = rawName ? rawName : L"";
    size_t slash = name.find_last_of(L"\\/");
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        name.erase(dot);
    }
    if (name.empty()) name = L"No shaders";
    return name;
}

static std::wstring ShaderToLowerCopy(const std::wstring& src)
{
    std::wstring out = src;
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return out;
}

static constexpr int kShaderNameBufferChars = 16384;

static void GetShaderNameAtIndex(TmUIData* d, int index, wchar_t* outName, int outNameMax)
{
    if (!outName || outNameMax <= 0) return;
    outName[0] = L'\0';

    wchar_t shaderNames[kShaderNameBufferChars] = L"No shaders";
    int count = GetShaderCountAndNames(d, shaderNames, kShaderNameBufferChars);
    if (count <= 0) {
        wcscpy_s(outName, outNameMax, L"No shaders");
        return;
    }

    if (index < 0) index = 0;
    if (index >= count) index = count - 1;

    wchar_t* ctx = nullptr;
    wchar_t* token = wcstok_s(shaderNames, L"|", &ctx);
    int idx = 0;
    while (token && idx < index) {
        token = wcstok_s(nullptr, L"|", &ctx);
        idx++;
    }

    if (token) {
        std::wstring display = ShaderDisplayNameFromRaw(token);
        wcscpy_s(outName, outNameMax, display.c_str());
    } else {
        wcscpy_s(outName, outNameMax, L"No shaders");
    }
}

// Build shader switcher row positioned directly under the layout canvas.
static void BuildLayoutShaderRowUnderCanvas(TmUIData* d, std::vector<SbButton>& out)
{
    out.clear();
    if (!d || d->curTab != TAB_LAYOUT) return;
    if (d->selectedPanel < 0 || d->selectedPanel >= d->numLayoutPanels) return;
    const OverlayPanel& p = d->layoutPanels[d->selectedPanel];
    if (p.panelType != 3) return; // Only for shader panels

    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };
    int pad = S(8);
    int x = d->rcCanvas.left;
    int y = d->rcCanvas.bottom + pad;
    int w = d->rcCanvas.right - d->rcCanvas.left;
    int bh = S(34);
    int gap = S(8);

    // If there's not enough space below canvas, try to anchor inside bottom of canvas with small overlap
    RECT client; GetClientRect(d->hWnd, &client);
    if (y + bh + pad > client.bottom) {
        y = max(d->rcCanvas.bottom - (bh + pad), d->rcCanvas.top);
    }

    int btnW = S(54);
    int nameW = w - btnW * 3 - gap * 2;
    if (nameW < S(120)) nameW = S(120);

    wchar_t shaderName[128] = L"No shaders";
    GetShaderNameAtIndex(d, p.shaderIndex, shaderName, 128);

    out.push_back({ { x, y, x + btnW, y + bh }, LB_SHADER_PREV, L"<", false, false });
    out.push_back({ { x + btnW + gap, y, x + btnW + gap + nameW, y + bh }, LB_SHADER_PICKER, shaderName, false, false });
    out.push_back({ { x + btnW + gap + nameW + gap, y, x + btnW * 2 + gap + nameW + gap, y + bh }, LB_SHADER_NEXT, L">", false, false });
    out.push_back({ { x + w - btnW, y, x + w, y + bh }, LB_SHADER_RELOAD, L"Reload", false, false });
}

static void SyncLayoutShaderPickerControls(TmUIData* d)
{
    if (!d || !d->hWnd) return;

    bool show = (d->curTab == TAB_LAYOUT && d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels &&
                 d->layoutPanels[d->selectedPanel].panelType == 3);
    if (!show) {
        return;
    }

    wchar_t shaderName[128] = L"No shaders";
    GetShaderNameAtIndex(d, d->layoutPanels[d->selectedPanel].shaderIndex, shaderName, 128);

    int panelX = d->rcSidebar.left + (int)(TmSpace::SM * d->dpi + 0.5f);
    int panelW = (d->rcSidebar.right - d->rcSidebar.left) - (int)(TmSpace::SM * d->dpi + 0.5f) * 2;
    int btnH = (int)(34 * d->dpi + 0.5f);
    int gap = (int)(8 * d->dpi + 0.5f);
    int btnW = (int)(54 * d->dpi + 0.5f);
    int nameW = panelW - btnW * 3 - gap * 2;
    if (nameW < (int)(120 * d->dpi + 0.5f)) nameW = (int)(120 * d->dpi + 0.5f);

    RECT rcPanel = d->rcSidebar;
    int y = rcPanel.top + (int)(TmSpace::SM * d->dpi + 0.5f);
    y += (int)(28 * d->dpi + 0.5f); // panel list header space
    y += (btnH + gap) * (d->numLayoutPanels + 1);
    y += gap + btnH * 4 + gap * 3; // add-panel card + position card + arrange card + spacing approximation

    HWND buttons[] = { nullptr, nullptr, nullptr, nullptr };
    buttons[0] = GetDlgItem(d->hWnd, LB_SHADER_PREV);
    buttons[1] = GetDlgItem(d->hWnd, LB_SHADER_PICKER);
    buttons[2] = GetDlgItem(d->hWnd, LB_SHADER_NEXT);
    buttons[3] = GetDlgItem(d->hWnd, LB_SHADER_RELOAD);

    RECT rPrev = { panelX, y, panelX + btnW, y + btnH };
    RECT rName = { panelX + btnW + gap, y, panelX + btnW + gap + nameW, y + btnH };
    RECT rNext = { panelX + btnW + gap + nameW + gap, y, panelX + btnW * 2 + gap + nameW + gap, y + btnH };
    RECT rReload = { rcPanel.right - (int)(TmSpace::SM * d->dpi + 0.5f) - btnW, y, rcPanel.right - (int)(TmSpace::SM * d->dpi + 0.5f), y + btnH };

    if (buttons[0]) SetWindowPos(buttons[0], HWND_TOP, rPrev.left, rPrev.top, rPrev.right - rPrev.left, rPrev.bottom - rPrev.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (buttons[1]) SetWindowPos(buttons[1], HWND_TOP, rName.left, rName.top, rName.right - rName.left, rName.bottom - rName.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (buttons[2]) SetWindowPos(buttons[2], HWND_TOP, rNext.left, rNext.top, rNext.right - rNext.left, rNext.bottom - rNext.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (buttons[3]) SetWindowPos(buttons[3], HWND_TOP, rReload.left, rReload.top, rReload.right - rReload.left, rReload.bottom - rReload.top, SWP_NOZORDER | SWP_NOACTIVATE);

    for (HWND h : buttons) {
        if (h) {
            ShowWindow(h, SW_SHOW);
            EnableWindow(h, TRUE);
        }
    }

    SetWindowTextW(buttons[1], shaderName);
}

static void HideLayoutShaderPickerControls(TmUIData* d)
{
    if (!d) return;
    HWND buttons[] = {
        GetDlgItem(d->hWnd, LB_SHADER_PREV),
        GetDlgItem(d->hWnd, LB_SHADER_PICKER),
        GetDlgItem(d->hWnd, LB_SHADER_NEXT),
        GetDlgItem(d->hWnd, LB_SHADER_RELOAD)
    };
    for (HWND h : buttons) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

static void ApplyShaderSelectionToPanel(TmUIData* d, int shaderIndex)
{
    if (!d || d->selectedPanel < 0 || d->selectedPanel >= d->numLayoutPanels) return;
    OverlayPanel& p = d->layoutPanels[d->selectedPanel];
    if (p.panelType != 3) return;

    wchar_t shaderNames[kShaderNameBufferChars] = L"No shaders";
    int shaderCount = GetShaderCountAndNames(d, shaderNames, kShaderNameBufferChars);
    if (shaderCount <= 0) return;

    if (shaderIndex < 0) shaderIndex = 0;
    if (shaderIndex >= shaderCount) shaderIndex = shaderCount - 1;

    p.shaderIndex = shaderIndex;
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    SyncLayoutShaderPickerControls(d);
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

struct ShaderPickerItem {
    std::wstring displayName;
    std::wstring lowerName;
    int shaderIndex = -1;
};

struct ShaderPickerData {
    TmUIData* ui = nullptr;
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND list = nullptr;
    HWND info = nullptr;
    HWND ok = nullptr;
    HWND cancel = nullptr;
    bool done = false;
    bool result = false;
    int chosenShaderIndex = -1;
    int currentShaderIndex = -1;
    std::vector<ShaderPickerItem> items;
};

static void ShaderPickerUpdateInfo(ShaderPickerData* data)
{
    if (!data || !data->info) return;

    int total = (int)data->items.size();
    int filtered = 0;
    if (data->list) filtered = (int)SendMessageW(data->list, LB_GETCOUNT, 0, 0);

    wchar_t text[256] = {};
    if (total <= 0) {
        wcscpy_s(text, _countof(text), L"No shaders found");
    } else if (filtered <= 0) {
        swprintf_s(text, L"No matches (%d shaders loaded)", total);
    } else {
        int sel = data->list ? (int)SendMessageW(data->list, LB_GETCURSEL, 0, 0) : LB_ERR;
        if (sel != LB_ERR) {
            wchar_t selected[128] = {};
            if (data->list) SendMessageW(data->list, LB_GETTEXT, sel, (LPARAM)selected);
            swprintf_s(text, L"%d shaders • %d visible • %ls", total, filtered, selected[0] ? selected : L"Selected");
        } else {
            swprintf_s(text, L"%d shaders • %d visible", total, filtered);
        }
    }
    SetWindowTextW(data->info, text);
}

static void ShaderPickerRebuildList(ShaderPickerData* data)
{
    if (!data || !data->list) return;

    wchar_t filterBuf[128] = {};
    if (data->edit) GetWindowTextW(data->edit, filterBuf, _countof(filterBuf));
    std::wstring filter = ShaderToLowerCopy(filterBuf);

    SendMessageW(data->list, LB_RESETCONTENT, 0, 0);

    int selectedRow = -1;
    for (const auto& item : data->items) {
        if (!filter.empty() && item.lowerName.find(filter) == std::wstring::npos) {
            continue;
        }

        int row = (int)SendMessageW(data->list, LB_ADDSTRING, 0, (LPARAM)item.displayName.c_str());
        if (row != LB_ERR && row != LB_ERRSPACE) {
            SendMessageW(data->list, LB_SETITEMDATA, row, (LPARAM)item.shaderIndex);
            if (item.shaderIndex == data->currentShaderIndex) {
                selectedRow = row;
            }
        }
    }

    int count = (int)SendMessageW(data->list, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        if (selectedRow < 0) selectedRow = 0;
        SendMessageW(data->list, LB_SETCURSEL, selectedRow, 0);
        if (data->ok) EnableWindow(data->ok, TRUE);
    } else {
        if (data->ok) EnableWindow(data->ok, FALSE);
    }

    ShaderPickerUpdateInfo(data);
}

static bool ShaderPickerLoadItems(ShaderPickerData* data)
{
    if (!data || !data->ui) return false;

    wchar_t shaderNames[16384] = L"No shaders";
    int shaderCount = GetShaderCountAndNames(data->ui, shaderNames, _countof(shaderNames));
    if (shaderCount <= 0) return false;

    data->items.clear();
    wchar_t* ctx = nullptr;
    wchar_t* token = wcstok_s(shaderNames, L"|", &ctx);
    int shaderIndex = 0;
    while (token) {
        ShaderPickerItem item;
        item.displayName = ShaderDisplayNameFromRaw(token);
        item.lowerName = ShaderToLowerCopy(item.displayName);
        item.shaderIndex = shaderIndex++;
        data->items.push_back(item);
        token = wcstok_s(nullptr, L"|", &ctx);
    }

    std::sort(data->items.begin(), data->items.end(), [](const ShaderPickerItem& a, const ShaderPickerItem& b) {
        if (a.lowerName != b.lowerName) return a.lowerName < b.lowerName;
        if (a.displayName != b.displayName) return a.displayName < b.displayName;
        return a.shaderIndex < b.shaderIndex;
    });

    return true;
}

static bool ShaderPickerApplySelection(ShaderPickerData* data)
{
    if (!data || !data->list) return false;
    int row = (int)SendMessageW(data->list, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR) return false;

    int shaderIndex = (int)SendMessageW(data->list, LB_GETITEMDATA, row, 0);
    if (shaderIndex < 0) return false;

    data->chosenShaderIndex = shaderIndex;
    data->result = true;
    data->done = true;
    if (data->hwnd) DestroyWindow(data->hwnd);
    return true;
}

static void ShaderPickerLayout(ShaderPickerData* data)
{
    if (!data || !data->hwnd) return;

    RECT rc = {};
    GetClientRect(data->hwnd, &rc);
    auto S = [&](int v){ return data->ui ? (int)(v * data->ui->dpi + 0.5f) : v; };

    int pad = S(14);
    int gap = S(10);
    int labelH = S(18);
    int editH = S(28);
    int buttonH = S(32);
    int footerY = rc.bottom - pad - buttonH;

    if (data->edit) MoveWindow(data->edit, pad, pad + labelH + gap, rc.right - pad * 2, editH, TRUE);
    if (data->info) MoveWindow(data->info, pad, footerY - gap - S(20), rc.right - pad * 2, S(20), TRUE);
    if (data->list) MoveWindow(data->list, pad, pad + labelH + gap + editH + gap, rc.right - pad * 2,
                               max(S(160), footerY - (pad + labelH + gap + editH + gap) - gap), TRUE);

    int buttonW = max(S(92), (rc.right - pad * 2 - gap) / 2);
    if (data->ok) MoveWindow(data->ok, pad, footerY, buttonW, buttonH, TRUE);
    if (data->cancel) MoveWindow(data->cancel, pad + buttonW + gap, footerY, buttonW, buttonH, TRUE);
}

static void ShowShaderPickerMenu(TmUIData* d)
{
    if (!d || d->selectedPanel < 0 || d->selectedPanel >= d->numLayoutPanels) return;
    OverlayPanel& p = d->layoutPanels[d->selectedPanel];
    if (p.panelType != 3) return;

    static bool registered = false;
    static const wchar_t* kClassName = L"TmShaderPickerWnd";
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            ShaderPickerData* data = (ShaderPickerData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            switch (msg) {
            case WM_NCCREATE: {
                CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
                data = (ShaderPickerData*)cs->lpCreateParams;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
                return TRUE;
            }
            case WM_CREATE: {
                if (!data) return -1;
                data->hwnd = hWnd;

                HFONT font = data->ui && data->ui->fonts.heading ? data->ui->fonts.heading : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                RECT rc = {};
                GetClientRect(hWnd, &rc);
                int pad = 14;
                int editY = pad + 22;

                CreateWindowExW(0, L"STATIC", L"Filter", WS_CHILD | WS_VISIBLE,
                    pad, pad, rc.right - pad * 2, 18, hWnd, nullptr, nullptr, nullptr);
                data->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    pad, editY, rc.right - pad * 2, 28, hWnd, (HMENU)IDC_SHADER_PICKER_FILTER, nullptr, nullptr);
                data->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                    pad, editY + 38, rc.right - pad * 2, 220, hWnd, (HMENU)IDC_SHADER_PICKER_LIST, nullptr, nullptr);
                data->info = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                    pad, 0, rc.right - pad * 2, 20, hWnd, (HMENU)IDC_SHADER_PICKER_INFO, nullptr, nullptr);
                data->ok = CreateWindowExW(0, L"BUTTON", L"Select", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    pad, 0, 96, 32, hWnd, (HMENU)IDOK, nullptr, nullptr);
                data->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    pad + 106, 0, 96, 32, hWnd, (HMENU)IDCANCEL, nullptr, nullptr);

                HWND controls[] = { data->edit, data->list, data->info, data->ok, data->cancel };
                for (HWND c : controls) {
                    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
                }

                if (!ShaderPickerLoadItems(data)) {
                    SetWindowTextW(data->info, L"No shaders found");
                    EnableWindow(data->ok, FALSE);
                } else {
                    data->currentShaderIndex = (data->ui->selectedPanel >= 0 && data->ui->selectedPanel < data->ui->numLayoutPanels)
                        ? data->ui->layoutPanels[data->ui->selectedPanel].shaderIndex : 0;
                    ShaderPickerRebuildList(data);
                }

                ShaderPickerLayout(data);
                SetFocus(data->edit ? data->edit : data->list);
                return 0;
            }
            case WM_SIZE:
                ShaderPickerLayout(data);
                return 0;
            case WM_COMMAND:
                if (!data) break;
                switch (LOWORD(wp)) {
                case IDC_SHADER_PICKER_FILTER:
                    if (HIWORD(wp) == EN_CHANGE) {
                        ShaderPickerRebuildList(data);
                    }
                    return 0;
                case IDC_SHADER_PICKER_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) {
                        ShaderPickerUpdateInfo(data);
                        EnableWindow(data->ok, SendMessageW(data->list, LB_GETCURSEL, 0, 0) != LB_ERR);
                    } else if (HIWORD(wp) == LBN_DBLCLK) {
                        ShaderPickerApplySelection(data);
                    }
                    return 0;
                case IDOK:
                    ShaderPickerApplySelection(data);
                    return 0;
                case IDCANCEL:
                    data->result = false;
                    data->done = true;
                    DestroyWindow(hWnd);
                    return 0;
                }
                break;
            case WM_CLOSE:
                if (data) {
                    data->result = false;
                    data->done = true;
                }
                DestroyWindow(hWnd);
                return 0;
            case WM_DESTROY:
                if (data) data->done = true;
                return 0;
            }
            return DefWindowProcW(hWnd, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        registered = true;
    }

    ShaderPickerData data;
    data.ui = d;
    data.currentShaderIndex = p.shaderIndex;

    RECT ownerRc = {};
    int width = 620;
    int height = 480;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (GetWindowRect(d->hWnd, &ownerRc)) {
        x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
        y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - height) / 2;
    }

    RECT rc = { 0, 0, width, height };
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;

    HWND box = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        L"Select Shader",
        style,
        x, y, width, height,
        d->hWnd,
        nullptr,
        GetModuleHandleW(nullptr),
        &data);
    if (!box) return;

    data.hwnd = box;
    if (d->hWnd) EnableWindow(d->hWnd, FALSE);
    ShowWindow(box, SW_SHOW);
    UpdateWindow(box);

    MSG msg = {};
    while (!data.done) {
        if (GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(box, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            break;
        }
    }

    if (d->hWnd) {
        EnableWindow(d->hWnd, TRUE);
        SetForegroundWindow(d->hWnd);
    }

    if (data.result && data.chosenShaderIndex >= 0) {
        ApplyShaderSelectionToPanel(d, data.chosenShaderIndex);
    }
}

static void AddLayoutPanel(TmUIData* d, int type)
{
    if (d->numLayoutPanels >= MAX_OVERLAY_PANELS) return;
    // Prevent adding more than one main panel (type 0)
    if (type == 0) {
        for (int i = 0; i < d->numLayoutPanels; ++i) {
            if (d->layoutPanels[i].panelType == 0) {
                // Main panel already exists, select it instead
                d->selectedPanel = i;
                InvalidateRect(d->hWnd, nullptr, FALSE);
                return;
            }
        }
    }
    OverlayPanel& p = d->layoutPanels[d->numLayoutPanels];
    ZeroMemory(&p, sizeof(OverlayPanel));
    p.panelType = type;
    float off = 0.04f * (d->numLayoutPanels % 5);
    p.x = 0.30f + off; p.y = 0.30f + off; p.w = 0.40f; p.h = 0.40f;
    p.visible = true;
    p.bgOpacity = (type == 4) ? 180 : 120;
    p.textColor = RGB(255, 255, 255);
    p.textAlpha = 255;
    p.textAlpha2 = 255;
    p.bgColor = (type == 4) ? RGB(18, 20, 30) : RGB(0, 0, 0);
    p.hasBgColor = (type == 4) ? true : false;
    p.fontSize = (type == 4) ? 28 : 32;
    wcscpy_s(p.fontName, L"Segoe UI");
    wcscpy_s(p.headingFontName, p.fontName);
    p.headingTextColor = p.textColor;
    p.headingTextColor2 = p.headingTextColor;
    p.headingTextAlpha = 255;
    p.headingTextAlpha2 = 255;
    wcscpy_s(p.songFontName, p.fontName);
    p.songTextColor = p.textColor;
    p.songTextColor2 = p.songTextColor;
    p.songTextAlpha = 255;
    p.songTextAlpha2 = 255;
    // Add default outline for text and song panels
    if (type == 2 || type == 4) {
        p.outlineWidth = 3;
        p.outlineColor = RGB(0, 0, 0);
    }
    if (type == 2) wcscpy_s(p.textContent, L"Text");
    else if (type == 4) {
        wcscpy_s(p.textContent, L"Now Playing:");
        p.rainbowColor = false;
        p.songAnimMode = 0; // Static
        p.textAlign = 1; // Center
    } else if (type == 3) {
        wchar_t shaderNames[kShaderNameBufferChars] = L"No shaders";
        int shaderCount = GetShaderCountAndNames(d, shaderNames, kShaderNameBufferChars);
        p.shaderIndex = (shaderCount > 0) ? min(d->sidebar.mainShaderIndex, shaderCount - 1) : 0;
    }
    p.zOrder = d->numLayoutPanels;
    d->selectedPanel = d->numLayoutPanels;
    d->numLayoutPanels++;

    // Sync to plugin
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TmUI::SaveState(d);
}

static void DeleteLayoutPanel(TmUIData* d, int idx)
{
    if (idx < 0 || idx >= d->numLayoutPanels) return;
    // Prevent deletion of main panel (type 0)
    if (d->layoutPanels[idx].panelType == 0) {
        TM_INFO("Cannot delete main panel");
        return;
    }
    ClearPanelImageCache(d, idx);
    for (int i = idx; i < d->numLayoutPanels - 1; ++i) {
        d->layoutPanels[i] = d->layoutPanels[i + 1];
        d->panelImageCache[i] = d->panelImageCache[i + 1];
    }
    d->panelImageCache[d->numLayoutPanels - 1] = nullptr;
    d->numLayoutPanels--;
    if (d->selectedPanel == idx) d->selectedPanel = -1;
    else if (d->selectedPanel > idx) d->selectedPanel--;

    // Sync to plugin
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TmUI::SaveState(d);
}

static void ApplyPanelPreset(TmUIData* d, int preset)
{
    if (d->selectedPanel < 0) return;
    OverlayPanel& p = d->layoutPanels[d->selectedPanel];
    if (preset == 9) { p.x = 0; p.y = 0; p.w = 1; p.h = 1; }
    else {
        int col = preset % 3, row = preset / 3;
        float cx[3] = { 0.0f, 0.5f - p.w * 0.5f, 1.0f - p.w };
        float cy[3] = { 0.0f, 0.5f - p.h * 0.5f, 1.0f - p.h };
        p.x = cx[col]; p.y = cy[row];
    }

    // Sync to plugin
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TmUI::SaveState(d);
}

static void ChangePanelZ(TmUIData* d, int delta)
{
    if (d->selectedPanel < 0) return;
    d->layoutPanels[d->selectedPanel].zOrder += delta;
    if (d->layoutPanels[d->selectedPanel].zOrder < 0) d->layoutPanels[d->selectedPanel].zOrder = 0;

    // Sync to plugin
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TmUI::SaveState(d);
}

static void BuildLayoutSidebar(TmUIData* d, std::vector<SbButton>& out)
{
    out.clear();
    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };
    RECT sb = d->rcSidebar;
    int pad = S(TmSpace::SM);
    int x = sb.left + pad, w = (sb.right - sb.left) - pad * 2;
    int y = sb.top + pad;
    int bh = S(32), gap = S(8);

    // PANEL LIST card - fixed height showing 5 items, scrollable
    y += S(28);
    int panelListTop = y;
    int panelListHeight = (bh + gap) * 5; // Show 5 items
    d->rcPanelList = { x, y, x + w, y + panelListHeight };
    
    for (int i = 0; i < d->numLayoutPanels; ++i) {
        const OverlayPanel& p = d->layoutPanels[i];
        // Build label: show full text safely
        wchar_t label[128] = {};
        if (p.panelType == 0) {
            // Main panel - fixed name
            swprintf_s(label, L"Main Output 0");
        } else if (p.panelType == 2) {
            // Text panel - safely get content with extra validation
            bool hasValidText = false;
            for (int j = 0; j < 40 && p.textContent[j] != L'\0'; ++j) {
                if (p.textContent[j] >= 32 && p.textContent[j] < 127) {
                    hasValidText = true;
                    break;
                }
            }
            if (hasValidText) {
                // Copy only printable ASCII characters
                wchar_t preview[48] = {};
                int dst = 0;
                for (int src = 0; src < 47 && p.textContent[src] != L'\0' && dst < 46; ++src) {
                    wchar_t c = p.textContent[src];
                    if (c >= 32 && c < 127) {
                        preview[dst++] = c;
                    }
                }
                swprintf_s(label, L"[%d] Text: %s", i+1, preview);
            } else {
                swprintf_s(label, L"[%d] Text", i+1);
            }
        } else if (p.panelType == 1) {
            if (p.hasImage) {
                // Check for valid path
                bool hasValidPath = false;
                for (int j = 0; j < 40 && p.imagePath[j] != L'\0'; ++j) {
                    if (p.imagePath[j] > 31) {
                        hasValidPath = true;
                        break;
                    }
                }
                if (hasValidPath) {
                    const wchar_t* fname = wcsrchr(p.imagePath, L'\\');
                    const wchar_t* name = fname ? fname+1 : p.imagePath;
                    // Copy filename safely
                    wchar_t preview[48] = {};
                    int dst = 0;
                    for (int src = 0; src < 47 && name[src] != L'\0' && dst < 46; ++src) {
                        wchar_t c = name[src];
                        if (c >= 32 && c < 127) {
                            preview[dst++] = c;
                        }
                    }
                    swprintf_s(label, L"[%d] Img: %s", i+1, preview);
                } else {
                    swprintf_s(label, L"[%d] Image", i+1);
                }
            } else {
                swprintf_s(label, L"[%d] Image", i+1);
            }
        } else if (p.panelType == 3) {
            wchar_t shaderName[128] = L"No shaders";
            GetShaderNameAtIndex(d, p.shaderIndex, shaderName, 128);
            swprintf_s(label, L"[%d] Shader: %s", i+1, shaderName);
        } else if (p.panelType == 4) {
            wchar_t nowPlaying[128] = {};
            GetNowPlayingText(d, nowPlaying, 128);
            if (nowPlaying[0]) {
                wchar_t preview[48] = {};
                int dst = 0;
                for (int src = 0; src < 47 && nowPlaying[src] != L'\0' && dst < 46; ++src) {
                    wchar_t c = nowPlaying[src];
                    if (c >= 32 && c < 127) preview[dst++] = c;
                }
                swprintf_s(label, L"[%d] Song: %s", i+1, preview);
            } else {
                swprintf_s(label, L"[%d] Song", i+1);
            }
        } else {
            const wchar_t* typeLabel = PanelTypeName(p.panelType);
            swprintf_s(label, L"[%d] %s", i+1, typeLabel);
        }
        bool selected = (d->selectedPanel == i);
        out.push_back({ { x, y, x + w, y + bh }, LB_PANEL_SELECT_BASE + i, label, selected, false });
        y += bh + gap;
    }
    y = panelListTop + panelListHeight + gap; // Move past the fixed panel list area

    // ADD PANEL card
    y += S(28);
    int half = (w - gap) / 2;
    out.push_back({ { x, y, x + half, y + bh }, LB_ADD_MAIN, L"+ Main", false, false });
    out.push_back({ { x + half + gap, y, x + w, y + bh }, LB_ADD_IMAGE, L"+ Image", false, false });
    y += bh + gap;
    out.push_back({ { x, y, x + half, y + bh }, LB_ADD_TEXT, L"+ Text", false, false });
    out.push_back({ { x + half + gap, y, x + w, y + bh }, LB_ADD_SHADER, L"+ Shader", false, false });
    y += bh + gap * 2;

    out.push_back({ { x, y, x + w, y + bh }, LB_ADD_SONG, L"+ Song / Now Playing", false, false });
    y += bh + gap * 2;

    // POSITION card
    y += S(28);
    int third = (w - gap * 2) / 3;
    const int posIds[9] = { LB_POS_TL, LB_POS_TC, LB_POS_TR, LB_POS_ML, LB_POS_MC, LB_POS_MR, LB_POS_BL, LB_POS_BC, LB_POS_BR };
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            int bx = x + c * (third + gap);
            out.push_back({ { bx, y, bx + third, y + bh }, posIds[r * 3 + c], L"", false, false });
        }
        y += bh + gap;
    }
    out.push_back({ { x, y, x + w, y + bh }, LB_POS_FULL, L"Full Screen", false, false });
    y += bh + gap * 2;

    // ARRANGE card
    y += S(28);
    out.push_back({ { x, y, x + half, y + bh }, LB_Z_UP, L"Bring Fwd", false, false });
    out.push_back({ { x + half + gap, y, x + w, y + bh }, LB_Z_DOWN, L"Send Back", false, false });
    y += bh + gap;
    out.push_back({ { x, y, x + w, y + bh }, LB_DELETE, L"Delete Panel", false, false });
    y += bh + gap;
    bool dsel = d->directSelectEnabled;
    out.push_back({ { x, y, x + w, y + bh }, LB_DIRECT_SELECT,
        dsel ? L"Canvas Select: ON" : L"Canvas Select: OFF", dsel, false });
    y += bh + gap * 2;

    // EVENT LAYOUTS card
    y += S(28);
    out.push_back({ { x, y, x + w, y + bh }, IDC_TM_LAYTAB_PRESET_SAVE, L"Save Event Layout...", false, false });
    y += bh + gap;
    out.push_back({ { x, y, x + w, y + bh }, IDC_TM_LAYTAB_PRESET_LOAD, L"Load Event Layout \x25BE", false, false });
    y += bh + gap * 2;

    // PANEL PROPERTIES card (only when panel selected)
    if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
        OverlayPanel& p = d->layoutPanels[d->selectedPanel];
        y += S(28);
        if (p.panelType == 1) { // Image panel
            out.push_back({ { x, y, x + w, y + bh }, LB_LOAD_IMAGE, L"Load Image...", false, false });
            y += bh + gap;
            int modeW = (w - gap * 4) / 5;
            const wchar_t* labels[5] = { L"Static", L"Rotate L/R", L"Spin", L"Orbit", L"Turbulence" };
            for (int i = 0; i < 5; ++i) {
                int bx = x + i * (modeW + gap);
                int id = LB_IMAGE_ANIM_0 + i;
                bool active = (p.imageAnimMode == i);
                out.push_back({ { bx, y, bx + modeW, y + bh }, id, labels[i], active, false });
            }
            y += bh + gap;
        } else if (p.panelType == 3) { // Shader panel
            // Shader switcher moved under the layout canvas; keep sidebar properties compact.
            y += bh + gap;
        } else if (p.panelType == 2 || p.panelType == 4) { // Text / Song panels
            // Text panels are edited via right-click on the panel itself.
            y += bh + gap;
        }
    }

    // Track total sidebar content height for full sidebar scrolling
    d->sidebarContentHeight = y - sb.top + pad;
}

// Text panel editor window (modal)
struct TextPanelEditorData {
    TmUIData* ui;
    int panelIdx;
    OverlayPanel original;
    OverlayPanel working;
    HWND hwnd;
    HWND editText;
    HWND lblFont;
    HWND lblSizeValue;
    HWND lblSizeHint;
    HWND lblOutlineValue;
    HWND lblAnimMode;
    HWND cmbAnimMode;
    HWND lblBounceSpeed;
    HWND cmbBounceSpeed;
    bool result;
    bool done;
};

static void ApplyTextPanelEditorPreview(TextPanelEditorData* data)
{
    if (!data || !data->ui || data->panelIdx < 0 || data->panelIdx >= data->ui->numLayoutPanels) return;
    data->ui->layoutPanels[data->panelIdx] = data->working;
    if (data->ui->host) {
        data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
    }
    InvalidateRect(data->ui->hWnd, nullptr, FALSE);
}

static void UpdateTextPanelEditorLabels(TextPanelEditorData* data)
{
    if (!data || !data->hwnd) return;
    wchar_t fontLabel[128];
    wchar_t sizeLabel[64];
    wchar_t outlineLabel[64];
    swprintf_s(fontLabel, L"Font: %ls", data->working.fontName[0] ? data->working.fontName : L"Arial");
    swprintf_s(sizeLabel, L"Size: %d", data->working.fontSize);
    swprintf_s(outlineLabel, L"Width: %d", data->working.outlineWidth);
    if (data->lblFont) SetWindowTextW(data->lblFont, fontLabel);
    if (data->lblSizeValue) SetWindowTextW(data->lblSizeValue, sizeLabel);
    if (data->lblOutlineValue) SetWindowTextW(data->lblOutlineValue, outlineLabel);
    if (data->cmbAnimMode) SendMessageW(data->cmbAnimMode, CB_SETCURSEL, max(0, min(data->working.textAnimMode, 5)), 0);
}

static void ReadTextPanelEditorText(TextPanelEditorData* data)
{
    if (!data || !data->editText) return;
    GetWindowTextW(data->editText, data->working.textContent, _countof(data->working.textContent));
}

static void TextPanelEditorLayoutControls(TextPanelEditorData* data)
{
    if (!data || !data->hwnd) return;

    RECT rc = {};
    GetClientRect(data->hwnd, &rc);
    auto S = [&](int v){ return (int)(v * (data->ui ? data->ui->dpi : 1.0f) + 0.5f); };

    int pad = S(16);
    int gap = S(8);
    int rowH = S(30);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    bool compact = w < S(660);

    int innerW = max(S(220), w - pad * 2);
    int editTop = pad + S(86);
    int editH = compact ? max(S(120), (h - editTop - pad) / 3) : max(S(140), (h - editTop - pad) / 3);
    int y = editTop + editH + gap;

    if (data->editText) MoveWindow(data->editText, pad, editTop, innerW, editH, TRUE);

    if (data->lblFont) MoveWindow(data->lblFont, pad, y, innerW, S(18), TRUE);
    if (data->lblSizeValue) MoveWindow(data->lblSizeValue, pad, y + S(20), innerW, S(18), TRUE);
    y += S(46);

    if (compact) {
        if (data->editText) {
            MoveWindow(data->editText, pad, editTop, innerW, editH, TRUE);
        }
        if (GetDlgItem(data->hwnd, 2001)) MoveWindow(GetDlgItem(data->hwnd, 2001), pad, y, innerW, rowH, TRUE);
        y += rowH + gap;
        if (GetDlgItem(data->hwnd, 2002)) MoveWindow(GetDlgItem(data->hwnd, 2002), pad, y, innerW, rowH, TRUE);
        y += rowH + gap;
        if (GetDlgItem(data->hwnd, 2003)) MoveWindow(GetDlgItem(data->hwnd, 2003), pad, y, innerW, rowH, TRUE);
        y += rowH + gap;

        int half2 = (innerW - gap) / 2;
        if (GetDlgItem(data->hwnd, 2006)) MoveWindow(GetDlgItem(data->hwnd, 2006), pad, y, half2, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2007)) MoveWindow(GetDlgItem(data->hwnd, 2007), pad + half2 + gap, y, half2, rowH, TRUE);
        y += rowH + gap;

        int half = (innerW - gap) / 2;
        if (GetDlgItem(data->hwnd, 2004)) MoveWindow(GetDlgItem(data->hwnd, 2004), pad, y, half, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2005)) MoveWindow(GetDlgItem(data->hwnd, 2005), pad + half + gap, y, half, rowH, TRUE);
        y += rowH + gap;

        if (data->lblSizeHint) MoveWindow(data->lblSizeHint, pad, y, innerW, S(18), TRUE);
        y += S(34);

        // Outline controls
        if (GetDlgItem(data->hwnd, 2008)) MoveWindow(GetDlgItem(data->hwnd, 2008), pad, y, innerW, rowH, TRUE);
        y += rowH + gap;
        int outlineBtnW = S(44);
        if (GetDlgItem(data->hwnd, 2009)) MoveWindow(GetDlgItem(data->hwnd, 2009), pad, y, outlineBtnW, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2010)) MoveWindow(GetDlgItem(data->hwnd, 2010), pad + outlineBtnW + gap, y, outlineBtnW, rowH, TRUE);
        if (data->lblOutlineValue) MoveWindow(data->lblOutlineValue, pad + (outlineBtnW + gap) * 2, y, innerW - (outlineBtnW + gap) * 2, S(18), TRUE);
        y += rowH + gap;

        if (data->lblAnimMode) MoveWindow(data->lblAnimMode, pad, y, innerW, S(18), TRUE);
        y += S(20);
        if (data->cmbAnimMode) MoveWindow(data->cmbAnimMode, pad, y, innerW, rowH, TRUE);
        y += rowH + gap;

        if (data->lblBounceSpeed) MoveWindow(data->lblBounceSpeed, pad, y, innerW, S(18), TRUE);
        y += S(20);
        if (data->cmbBounceSpeed) MoveWindow(data->cmbBounceSpeed, pad, y, innerW, rowH, TRUE);
        y += rowH + gap;

        int footerW = (innerW - gap) / 2;
        int footerY = max(y, h - pad - rowH);
        if (GetDlgItem(data->hwnd, IDOK)) MoveWindow(GetDlgItem(data->hwnd, IDOK), pad, footerY, footerW, rowH, TRUE);
        if (GetDlgItem(data->hwnd, IDCANCEL)) MoveWindow(GetDlgItem(data->hwnd, IDCANCEL), pad + footerW + gap, footerY, footerW, rowH, TRUE);
    } else {
        int third = (innerW - gap * 2) / 3;
        if (GetDlgItem(data->hwnd, 2001)) MoveWindow(GetDlgItem(data->hwnd, 2001), pad, y, third, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2002)) MoveWindow(GetDlgItem(data->hwnd, 2002), pad + third + gap, y, third, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2003)) MoveWindow(GetDlgItem(data->hwnd, 2003), pad + (third + gap) * 2, y, third, rowH, TRUE);
        y += rowH + gap;

        int twoThirds = (innerW - gap) / 2;
        if (GetDlgItem(data->hwnd, 2006)) MoveWindow(GetDlgItem(data->hwnd, 2006), pad, y, twoThirds, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2007)) MoveWindow(GetDlgItem(data->hwnd, 2007), pad + twoThirds + gap, y, twoThirds, rowH, TRUE);
        y += rowH + gap;

        int sizeBtnW = S(44);
        if (GetDlgItem(data->hwnd, 2004)) MoveWindow(GetDlgItem(data->hwnd, 2004), pad, y, sizeBtnW, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2005)) MoveWindow(GetDlgItem(data->hwnd, 2005), pad + sizeBtnW + gap, y, sizeBtnW, rowH, TRUE);
        if (data->lblSizeHint) MoveWindow(data->lblSizeHint, pad + (sizeBtnW + gap) * 2, y, innerW - (sizeBtnW + gap) * 2, S(18), TRUE);
        y += rowH + gap;

        // Outline controls
        int outlineBtnW = S(44);
        if (GetDlgItem(data->hwnd, 2008)) MoveWindow(GetDlgItem(data->hwnd, 2008), pad, y, S(108), rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2009)) MoveWindow(GetDlgItem(data->hwnd, 2009), pad + S(108) + gap, y, outlineBtnW, rowH, TRUE);
        if (GetDlgItem(data->hwnd, 2010)) MoveWindow(GetDlgItem(data->hwnd, 2010), pad + S(108) + gap + outlineBtnW + gap, y, outlineBtnW, rowH, TRUE);
        if (data->lblOutlineValue) MoveWindow(data->lblOutlineValue, pad + S(108) + gap + (outlineBtnW + gap) * 2, y, innerW - S(108) - gap - (outlineBtnW + gap) * 2, S(18), TRUE);
        y += rowH + gap;

        if (data->lblAnimMode) MoveWindow(data->lblAnimMode, pad, y, innerW, S(18), TRUE);
        y += S(20);
        if (data->cmbAnimMode) MoveWindow(data->cmbAnimMode, pad, y, innerW, rowH, TRUE);
        y += rowH + gap;

        if (data->lblBounceSpeed) MoveWindow(data->lblBounceSpeed, pad, y, innerW, S(18), TRUE);
        y += S(20);
        if (data->cmbBounceSpeed) MoveWindow(data->cmbBounceSpeed, pad, y, innerW, rowH, TRUE);
        y += rowH + gap;

        int footerW = S(92);
        int footerY = max(y, h - pad - rowH);
        if (GetDlgItem(data->hwnd, IDOK)) MoveWindow(GetDlgItem(data->hwnd, IDOK), w - pad * 2 - footerW * 2 - gap, footerY, footerW, rowH, TRUE);
        if (GetDlgItem(data->hwnd, IDCANCEL)) MoveWindow(GetDlgItem(data->hwnd, IDCANCEL), w - pad * 2 - footerW, footerY, footerW, rowH, TRUE);
    }
}

static bool ChoosePanelFont(HWND owner, OverlayPanel& p)
{
    LOGFONTW lf = {};
    wcscpy_s(lf.lfFaceName, p.fontName[0] ? p.fontName : L"Arial");

    HDC hdc = GetDC(owner);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(owner, hdc);

    lf.lfHeight = -MulDiv(p.fontSize > 0 ? p.fontSize : 32, dpiY, 72);
    lf.lfWeight = FW_NORMAL;

    CHOOSEFONTW cf = { sizeof(cf) };
    cf.hwndOwner = owner;
    cf.lpLogFont = &lf;
    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS;
    cf.rgbColors = p.textColor;

    if (!ChooseFontW(&cf)) return false;

    wcscpy_s(p.fontName, lf.lfFaceName);
    int newSize = (int)roundf((float)(-lf.lfHeight) * 72.0f / (float)dpiY);
    if (newSize < 8) newSize = 8;
    if (newSize > 200) newSize = 200;
    p.fontSize = newSize;
    p.textColor = cf.rgbColors;
    return true;
}

static bool ChoosePanelColor(HWND owner, COLORREF& color)
{
    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = owner;
    cc.rgbResult = color;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    static COLORREF customColors[16] = {};
    cc.lpCustColors = customColors;
    if (ChooseColorW(&cc)) {
        color = cc.rgbResult;
        return true;
    }
    return false;
}

struct AlphaPickerData {
    HWND hwnd = nullptr;
    HWND slider = nullptr;
    HWND alphaLabel = nullptr;
    COLORREF color = 0;
    BYTE alpha = 255;
    bool done = false;
    bool result = false;
};

static void AlphaPickerUpdate(AlphaPickerData* data)
{
    if (!data) return;
    wchar_t buf[64];
    swprintf_s(buf, L"Alpha: %u / 255", (unsigned)data->alpha);
    if (data->alphaLabel) SetWindowTextW(data->alphaLabel, buf);
    if (data->slider) SendMessageW(data->slider, TBM_SETPOS, TRUE, data->alpha);
    if (data->hwnd) InvalidateRect(data->hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK AlphaPickerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    AlphaPickerData* data = (AlphaPickerData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    static HBRUSH sDlgBgBrush = nullptr;
    static COLORREF sDlgBg = RGB(14, 16, 24);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        data = (AlphaPickerData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    }
    case WM_CREATE: {
        if (!sDlgBgBrush) sDlgBgBrush = CreateSolidBrush(sDlgBg);

        RECT rc = {};
        GetClientRect(hWnd, &rc);
        int pad = 12;
        int rowH = 24;
        int sliderY = 108;
        int btnY = rc.bottom - pad - 28;

        CreateWindowExW(0, L"STATIC", L"Transparency", WS_CHILD | WS_VISIBLE,
            pad, pad, rc.right - pad * 2, 18, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"Preview", WS_CHILD | WS_VISIBLE,
            pad, pad + 24, 80, 18, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"Alpha", WS_CHILD | WS_VISIBLE,
            pad, sliderY, 60, 18, hWnd, nullptr, nullptr, nullptr);

        data->slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
            pad + 54, sliderY - 2, rc.right - pad * 2 - 54, rowH + 6, hWnd, (HMENU)200, nullptr, nullptr);
        SendMessageW(data->slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
        SendMessageW(data->slider, TBM_SETPOS, TRUE, data->alpha);
        SendMessageW(data->slider, TBM_SETTICFREQ, 32, 0);

        data->alphaLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            pad, sliderY + 28, rc.right - pad * 2, 18, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            rc.right - pad * 2 - 180, btnY, 80, 28, hWnd, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            rc.right - pad * 2 - 92, btnY, 80, 28, hWnd, (HMENU)IDCANCEL, nullptr, nullptr);

        AlphaPickerUpdate(data);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(220, 228, 244));
        return (LRESULT)sDlgBgBrush;
    case WM_HSCROLL:
        if (data && (HWND)lp == data->slider) {
            data->alpha = (BYTE)SendMessageW(data->slider, TBM_GETPOS, 0, 0);
            AlphaPickerUpdate(data);
        }
        return 0;
    case WM_COMMAND:
        if (!data) break;
        switch (LOWORD(wp)) {
        case IDOK:
            data->result = true;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        case IDCANCEL:
            data->result = false;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc = {};
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, sDlgBgBrush);

        RECT preview = { 12, 56, rc.right - 12, 100 };
        const int cell = 10;
        for (int y = preview.top; y < preview.bottom; y += cell) {
            for (int x = preview.left; x < preview.right; x += cell) {
                bool dark = (((x - preview.left) / cell) + ((y - preview.top) / cell)) % 2 == 0;
                HBRUSH br = CreateSolidBrush(dark ? RGB(72, 76, 88) : RGB(104, 110, 124));
                RECT c = { x, y, min(x + cell, preview.right), min(y + cell, preview.bottom) };
                FillRect(hdc, &c, br);
                DeleteObject(br);
            }
        }

        Gdiplus::Graphics g(hdc);
        Gdiplus::Color fillColor(data->alpha, GetRValue(data->color), GetGValue(data->color), GetBValue(data->color));
        Gdiplus::SolidBrush brush(fillColor);
        g.FillRectangle(&brush, preview.left, preview.top, preview.right - preview.left, preview.bottom - preview.top);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        if (data) {
            data->result = false;
            data->done = true;
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (data) data->done = true;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static bool ChoosePanelAlpha(HWND owner, COLORREF color, BYTE& alpha)
{
    EnsureCommonControlsInit();

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = AlphaPickerWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TmAlphaPicker";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        registered = true;
    }

    AlphaPickerData data;
    data.color = color;
    data.alpha = alpha;

    RECT rc = { 0, 0, 380, 200 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_DLGMODALFRAME);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    RECT ownerRc = {};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (owner && GetWindowRect(owner, &ownerRc)) {
        x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
        y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - height) / 2;
    }

    HWND box = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"TmAlphaPicker",
        L"Choose Transparency",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &data);

    if (!box) return false;

    data.hwnd = box;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(box, SW_SHOW);
    UpdateWindow(box);

    MSG msg = {};
    while (!data.done) {
        if (GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(box, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    if (data.result) alpha = data.alpha;
    return data.result;
}

static bool ChoosePanelColorRGBA(HWND owner, COLORREF& color, BYTE& alpha)
{
    COLORREF originalColor = color;
    BYTE originalAlpha = alpha;
    COLORREF chosen = color;
    if (!ChoosePanelColor(owner, chosen)) {
        return false;
    }
    color = chosen;
    if (!ChoosePanelAlpha(owner, color, alpha)) {
        color = originalColor;
        alpha = originalAlpha;
        return false;
    }
    return true;
}

static bool ParseBankColor(const wchar_t* text, COLORREF& color)
{
    if (!text || !text[0]) return false;
    unsigned int r = 0, g = 0, b = 0;
    const wchar_t* s = text;
    if (*s == L'#') ++s;
    if (swscanf_s(s, L"%2x%2x%2x", &r, &g, &b) == 3) {
        color = RGB((BYTE)r, (BYTE)g, (BYTE)b);
        return true;
    }
    return false;
}

static void SaveBankConfig(TmUIData* d)
{
    if (!d) return;
    wchar_t path[MAX_PATH];
    BankConfigPath(path, MAX_PATH);
    for (int i = 0; i < NUM_BANKS; ++i) {
        wchar_t section[32];
        wchar_t colorText[32];
        swprintf_s(section, L"Bank%02d", i + 1);
        swprintf_s(colorText, L"#%02X%02X%02X", GetRValue(d->grid.bankColors[i]), GetGValue(d->grid.bankColors[i]), GetBValue(d->grid.bankColors[i]));
        WritePrivateProfileStringW(section, L"Name", d->grid.bankNames[i], path);
        WritePrivateProfileStringW(section, L"Color", colorText, path);
    }
}

static void SaveKaraokePrefs(TmUIData* d)
{
    if (!d) return;
    wchar_t path[MAX_PATH];
    KaraokePrefsPath(path, MAX_PATH);
    WritePrivateProfileStringW(L"Karaoke", L"AutoHide", d->karaokeAutoHide ? L"1" : L"0", path);
}

static void LayoutProfilesPath(wchar_t* out, int cap)
{
    wchar_t base[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, base);
    CreateDirectoryW(base, nullptr);
    wcscpy_s(out, cap, base);
    wcscat_s(out, cap, L"\\TellyMedia-reborn");
    CreateDirectoryW(out, nullptr);
    wcscat_s(out, cap, L"\\EventLayouts");
    CreateDirectoryW(out, nullptr);
}

static bool PromptLayoutProfileFile(HWND owner, wchar_t* outPath, bool saveFile)
{
    if (!outPath) return false;
    outPath[0] = L'\0';

    wchar_t initialDir[MAX_PATH] = {};
    LayoutProfilesPath(initialDir, MAX_PATH);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"TellyMedia Layout Profiles (*.tmlayout)\0*.tmlayout\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrInitialDir = initialDir;
    ofn.lpstrDefExt = L"tmlayout";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (saveFile) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        return GetSaveFileNameW(&ofn) != FALSE;
    }
    ofn.Flags |= OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&ofn) != FALSE;
}

static bool SaveLayoutProfileToFile(TmUIData* d, const wchar_t* path)
{
    if (!d || !path || !path[0]) return false;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    DWORD wr = 0;
    DWORD magic = 0x504C4D54; // 'TMLP'
    DWORD version = 1;
    int count = d->numLayoutPanels;
    if (count < 0) count = 0;
    if (count > MAX_OVERLAY_PANELS) count = MAX_OVERLAY_PANELS;
    int selected = d->selectedPanel;
    BYTE directSelect = d->directSelectEnabled ? 1 : 0;

    WriteFile(f, &magic, sizeof(magic), &wr, nullptr);
    WriteFile(f, &version, sizeof(version), &wr, nullptr);
    WriteFile(f, &count, sizeof(count), &wr, nullptr);
    WriteFile(f, &selected, sizeof(selected), &wr, nullptr);
    WriteFile(f, &directSelect, sizeof(directSelect), &wr, nullptr);
    WriteFile(f, d->layoutPanels, sizeof(OverlayPanel) * count, &wr, nullptr);
    CloseHandle(f);
    TM_INFO("Saved layout profile: %S", path);
    TmUI::SaveState(d);
    return true;
}

static bool LoadLayoutProfileFromFile(TmUIData* d, const wchar_t* path)
{
    if (!d || !path || !path[0]) return false;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    DWORD rd = 0;
    DWORD magic = 0;
    DWORD version = 0;
    int count = 0;
    int selected = -1;
    BYTE directSelect = 0;
    OverlayPanel tempPanels[MAX_OVERLAY_PANELS] = {};
    if (!ReadFile(f, &magic, sizeof(magic), &rd, nullptr) || rd != sizeof(magic) || magic != 0x504C4D54) {
        CloseHandle(f);
        return false;
    }
    if (!ReadFile(f, &version, sizeof(version), &rd, nullptr) || rd != sizeof(version) || version != 1) {
        CloseHandle(f);
        return false;
    }
    if (!ReadFile(f, &count, sizeof(count), &rd, nullptr) || rd != sizeof(count)) {
        CloseHandle(f);
        return false;
    }
    if (!ReadFile(f, &selected, sizeof(selected), &rd, nullptr) || rd != sizeof(selected)) {
        CloseHandle(f);
        return false;
    }
    if (!ReadFile(f, &directSelect, sizeof(directSelect), &rd, nullptr) || rd != sizeof(directSelect)) {
        CloseHandle(f);
        return false;
    }

    if (count < 0) count = 0;
    if (count > MAX_OVERLAY_PANELS) count = MAX_OVERLAY_PANELS;

    for (int i = 0; i < count; ++i) {
        if (!ReadFile(f, &tempPanels[i], sizeof(OverlayPanel), &rd, nullptr) || rd != sizeof(OverlayPanel)) {
            CloseHandle(f);
            return false;
        }
        if (tempPanels[i].panelType < 0 || tempPanels[i].panelType > 4) tempPanels[i].panelType = 0;
        if (tempPanels[i].panelType == 1 && tempPanels[i].imagePath[0] == L'\0') {
            tempPanels[i].hasImage = false;
        }
    }

    for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) ClearPanelImageCache(d, i);
    ZeroMemory(d->layoutPanels, sizeof(d->layoutPanels));
    for (int i = 0; i < count; ++i) d->layoutPanels[i] = tempPanels[i];

    d->numLayoutPanels = count;
    d->selectedPanel = (selected >= 0 && selected < count) ? selected : (count > 0 ? 0 : -1);
    d->directSelectEnabled = (directSelect != 0);

    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TmUI::SaveState(d);
    InvalidateRect(d->hWnd, nullptr, FALSE);
    CloseHandle(f);
    TM_INFO("Loaded layout profile: %S", path);
    return true;
}

struct LayoutProfileEntry {
    wchar_t path[MAX_PATH];
    wchar_t label[128];
};

static void CollectLayoutProfiles(std::vector<LayoutProfileEntry>& out)
{
    out.clear();

    wchar_t dir[MAX_PATH] = {};
    LayoutProfilesPath(dir, MAX_PATH);

    wchar_t pattern[MAX_PATH] = {};
    swprintf_s(pattern, L"%s\\*.tmlayout", dir);

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        LayoutProfileEntry e = {};
        swprintf_s(e.path, L"%s\\%s", dir, fd.cFileName);
        wcscpy_s(e.label, fd.cFileName);
        wchar_t* dot = wcsrchr(e.label, L'.');
        if (dot && _wcsicmp(dot, L".tmlayout") == 0) *dot = L'\0';
        out.push_back(e);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    for (size_t i = 1; i < out.size(); ++i) {
        LayoutProfileEntry key = out[i];
        size_t j = i;
        while (j > 0 && _wcsicmp(out[j - 1].label, key.label) > 0) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
}

static void ShowLayoutProfileMenu(TmUIData* d)
{
    if (!d || !d->hWnd) return;

    std::vector<LayoutProfileEntry> profiles;
    CollectLayoutProfiles(profiles);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const UINT kBaseId = 50000;
    const UINT kBrowseId = 51000;

    if (profiles.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, kBaseId, L"(No saved event layouts)");
    } else {
        for (size_t i = 0; i < profiles.size(); ++i) {
            AppendMenuW(menu, MF_STRING, kBaseId + (UINT)i, profiles[i].label);
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kBrowseId, L"Browse for layout file...");

    POINT pt = {};
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                             pt.x, pt.y, 0, d->hWnd, nullptr);
    DestroyMenu(menu);

    if (cmd >= (int)kBaseId && cmd < (int)(kBaseId + profiles.size())) {
        LoadLayoutProfileFromFile(d, profiles[(size_t)(cmd - (int)kBaseId)].path);
    } else if (cmd == (int)kBrowseId) {
        wchar_t path[MAX_PATH] = {};
        if (PromptLayoutProfileFile(d->hWnd, path, false)) {
            LoadLayoutProfileFromFile(d, path);
        }
    }
}

static void LoadBankConfig(TmUIData* d)
{
    if (!d) return;
    wchar_t path[MAX_PATH];
    BankConfigPath(path, MAX_PATH);
    for (int i = 0; i < NUM_BANKS; ++i) {
        wchar_t section[32];
        wchar_t text[64];
        wchar_t value[64];
        swprintf_s(section, L"Bank%02d", i + 1);

        GetPrivateProfileStringW(section, L"Name", d->grid.bankNames[i], text, _countof(text), path);
        wcscpy_s(d->grid.bankNames[i], text);

        GetPrivateProfileStringW(section, L"Color", L"", value, _countof(value), path);
        COLORREF parsed = d->grid.bankColors[i];
        if (ParseBankColor(value, parsed)) {
            d->grid.bankColors[i] = parsed;
        }
    }
}

enum { IDC_BANK_LIST = 5000, IDC_BANK_NAME = 5001, IDC_BANK_COLOR = 5002 };

struct BankConfigData {
    TmUIData* ui = nullptr;
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND edit = nullptr;
    HWND colorPreview = nullptr;
    HWND colorButton = nullptr;
    int selectedBank = 0;
    bool done = false;
    bool result = false;
    wchar_t workingNames[NUM_BANKS][32] = {};
    COLORREF workingColors[NUM_BANKS] = {};
    HBRUSH previewBrush = nullptr;
};

enum { IDC_LICENSE_EMAIL = 5100, IDC_LICENSE_PASSWORD = 5101, IDC_LICENSE_SAVE = 5102 };

struct LicenseLoginData {
    TmUIData* ui = nullptr;
    HWND hwnd = nullptr;
    HWND editEmail = nullptr;
    HWND editPassword = nullptr;
    HWND chkSave = nullptr;
    bool done = false;
    bool result = false;
};

static void LicenseStatusText(TmUIData* d, wchar_t* out, int outMax)
{
    if (!out || outMax <= 0) return;
    out[0] = L'\0';

    LicenseState st = {};
    TmLicense::Init(&st);
    if (!d || !d->host || !d->host->HostGetLicenseState(&st)) {
        wcscpy_s(out, outMax, L"License status unavailable");
        return;
    }

    const wchar_t* status = L"Unknown";
    switch (st.status) {
    case LIC_UNCHECKED: status = L"Unchecked"; break;
    case LIC_VALID: status = L"Active"; break;
    case LIC_INVALID: status = L"Invalid"; break;
    case LIC_EXPIRED: status = L"Expired"; break;
    case LIC_REVOKED: status = L"Revoked"; break;
    case LIC_ERROR: status = L"Error"; break;
    }

    if (st.userEmail[0]) {
        swprintf_s(out, outMax, L"%s — %s", st.userEmail, status);
    } else if (st.licenseKey[0]) {
        swprintf_s(out, outMax, L"%hs — %s", st.licenseKey, status);
    } else {
        swprintf_s(out, outMax, L"No stored license — %s", status);
    }

    if (st.message[0]) {
        size_t len = wcslen(out);
        if (len + 4 < (size_t)outMax) {
            wcscat_s(out, outMax, L"\n");
            wchar_t msg[128] = {};
            MultiByteToWideChar(CP_UTF8, 0, st.message, -1, msg, _countof(msg));
            wcscat_s(out, outMax, msg);
        }
    }
}

static void LicenseBadgeInfo(TmUIData* d, wchar_t* out, int outMax, COLORREF* bg, COLORREF* fg)
{
    if (!out || outMax <= 0) return;
    out[0] = L'\0';

    if (bg) *bg = TmColor::BG_CARD;
    if (fg) *fg = TmColor::TEXT_SECOND;

    LicenseState st = {};
    TmLicense::Init(&st);
    if (!d || !d->host || !d->host->HostGetLicenseState(&st)) {
        wcscpy_s(out, outMax, L"License: Unavailable");
        return;
    }

    const wchar_t* status = L"Unknown";
    switch (st.status) {
    case LIC_UNCHECKED: status = L"Unchecked"; break;
    case LIC_VALID: status = L"Active"; break;
    case LIC_INVALID: status = L"Invalid"; break;
    case LIC_EXPIRED: status = L"Expired"; break;
    case LIC_REVOKED: status = L"Revoked"; break;
    case LIC_ERROR: status = L"Error"; break;
    }

    swprintf_s(out, outMax, L"License: %s", status);

    if (st.licensed || st.status == LIC_VALID) {
        if (bg) *bg = RGB(35, 88, 58);
        if (fg) *fg = RGB(232, 250, 240);
    } else if (st.status == LIC_EXPIRED || st.status == LIC_REVOKED) {
        if (bg) *bg = RGB(94, 39, 39);
        if (fg) *fg = RGB(255, 236, 236);
    } else if (st.status == LIC_ERROR || st.status == LIC_INVALID) {
        if (bg) *bg = RGB(103, 68, 24);
        if (fg) *fg = RGB(255, 244, 214);
    }
}

// Convert US date format (MM/DD/YYYY) to UK format (DD/MM/YYYY)
static void ConvertToUKDate(const char* usDate, wchar_t* ukDate, int maxLen)
{
    ukDate[0] = L'\0';
    if (!usDate || !usDate[0]) {
        wcscpy_s(ukDate, maxLen, L"N/A");
        return;
    }
    
    // Parse MM/DD/YYYY format
    int month = 0, day = 0, year = 0;
    if (sscanf_s(usDate, "%d/%d/%d", &month, &day, &year) == 3) {
        // Convert to DD/MM/YYYY
        swprintf_s(ukDate, maxLen, L"%02d/%02d/%04d", day, month, year);
    } else {
        // If parsing fails, just convert as-is
        MultiByteToWideChar(CP_UTF8, 0, usDate, -1, ukDate, maxLen);
    }
}

// Calculate days remaining from expiry date (MM/DD/YYYY format)
static int CalculateDaysRemaining(const char* expiryDate)
{
    if (!expiryDate || !expiryDate[0]) return -1;
    
    int month = 0, day = 0, year = 0;
    if (sscanf_s(expiryDate, "%d/%d/%d", &month, &day, &year) != 3) return -1;
    
    // Get current time
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    // Create tm structure for expiry date
    struct tm expiryTm = {};
    expiryTm.tm_year = year - 1900;
    expiryTm.tm_mon = month - 1;
    expiryTm.tm_mday = day;
    expiryTm.tm_hour = 0;
    expiryTm.tm_min = 0;
    expiryTm.tm_sec = 0;
    expiryTm.tm_isdst = -1;
    
    // Create tm structure for current date
    struct tm currentTm = {};
    currentTm.tm_year = st.wYear - 1900;
    currentTm.tm_mon = st.wMonth - 1;
    currentTm.tm_mday = st.wDay;
    currentTm.tm_hour = 0;
    currentTm.tm_min = 0;
    currentTm.tm_sec = 0;
    currentTm.tm_isdst = -1;
    
    time_t expiryTime = mktime(&expiryTm);
    time_t currentTime = mktime(&currentTm);
    
    if (expiryTime == -1 || currentTime == -1) return -1;
    
    double diffSeconds = difftime(expiryTime, currentTime);
    int daysRemaining = (int)(diffSeconds / (60 * 60 * 24));
    
    return daysRemaining;
}

static void LicenseStatusDetails(TmUIData* d, wchar_t* out, int outMax)
{
    if (!out || outMax <= 0) return;
    out[0] = L'\0';

    LicenseState st = {};
    TmLicense::Init(&st);
    if (!d || !d->host || !d->host->HostGetLicenseState(&st)) return;

    if (st.message[0]) {
        wchar_t msg[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, st.message, -1, msg, _countof(msg));
        wcscpy_s(out, outMax, msg);
    } else if (st.status == LIC_VALID) {
        wchar_t expiry[64] = {};
        int licDaysLeft = -1;
        bool isLifetime = false;
        if (st.licenseType[0] && (_stricmp(st.licenseType, "lifetime") == 0 || _stricmp(st.licenseType, "Lifetime") == 0)) {
            wcscpy_s(expiry, L"Unlimited");
            isLifetime = true;
        } else if (st.licExpiry[0] && strcmp(st.licExpiry, "NULL") != 0 && strcmp(st.licExpiry, "null") != 0) {
            ConvertToUKDate(st.licExpiry, expiry, _countof(expiry));
            licDaysLeft = CalculateDaysRemaining(st.licExpiry);
        }
        wchar_t tokenExpiry[64] = {};
        int tokenDaysLeft = -1;
        if (st.tokenExpiry[0] && strcmp(st.tokenExpiry, "NULL") != 0 && strcmp(st.tokenExpiry, "null") != 0) {
            ConvertToUKDate(st.tokenExpiry, tokenExpiry, _countof(tokenExpiry));
            tokenDaysLeft = CalculateDaysRemaining(st.tokenExpiry);
        }
        wchar_t details[512] = {};
        if (expiry[0] && tokenExpiry[0] && st.maxActivations > 0) {
            if (isLifetime && tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License: %s. Account expires: %s (%d days). Devices: %d/%d", expiry, tokenExpiry, tokenDaysLeft, st.currentActivations, st.maxActivations);
            } else if (isLifetime) {
                swprintf_s(details, _countof(details), L"Logged in and active. License: %s. Account expires: %s. Devices: %d/%d", expiry, tokenExpiry, st.currentActivations, st.maxActivations);
            } else if (licDaysLeft >= 0 && tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s (%d days). Account expires: %s (%d days). Devices: %d/%d", expiry, licDaysLeft, tokenExpiry, tokenDaysLeft, st.currentActivations, st.maxActivations);
            } else if (licDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s (%d days). Account expires: %s. Devices: %d/%d", expiry, licDaysLeft, tokenExpiry, st.currentActivations, st.maxActivations);
            } else if (tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s. Account expires: %s (%d days). Devices: %d/%d", expiry, tokenExpiry, tokenDaysLeft, st.currentActivations, st.maxActivations);
            } else {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s. Account expires: %s. Devices: %d/%d", expiry, tokenExpiry, st.currentActivations, st.maxActivations);
            }
        } else if (expiry[0] && tokenExpiry[0]) {
            if (isLifetime && tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License: %s. Account expires: %s (%d days)", expiry, tokenExpiry, tokenDaysLeft);
            } else if (isLifetime) {
                swprintf_s(details, _countof(details), L"Logged in and active. License: %s. Account expires: %s", expiry, tokenExpiry);
            } else if (licDaysLeft >= 0 && tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s (%d days). Account expires: %s (%d days)", expiry, licDaysLeft, tokenExpiry, tokenDaysLeft);
            } else if (licDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s (%d days). Account expires: %s", expiry, licDaysLeft, tokenExpiry);
            } else if (tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s. Account expires: %s (%d days)", expiry, tokenExpiry, tokenDaysLeft);
            } else {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s. Account expires: %s", expiry, tokenExpiry);
            }
        } else if (expiry[0] && st.maxActivations > 0) {
            if (isLifetime) {
                swprintf_s(details, _countof(details), L"Logged in and active. License: %s. Devices: %d/%d", expiry, st.currentActivations, st.maxActivations);
            } else if (licDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s (%d days). Devices: %d/%d", expiry, licDaysLeft, st.currentActivations, st.maxActivations);
            } else {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s. Devices: %d/%d", expiry, st.currentActivations, st.maxActivations);
            }
        } else if (tokenExpiry[0] && st.maxActivations > 0) {
            if (tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. Account expires: %s (%d days). Devices: %d/%d", tokenExpiry, tokenDaysLeft, st.currentActivations, st.maxActivations);
            } else {
                swprintf_s(details, _countof(details), L"Logged in and active. Account expires: %s. Devices: %d/%d", tokenExpiry, st.currentActivations, st.maxActivations);
            }
        } else if (expiry[0]) {
            if (isLifetime) {
                swprintf_s(details, _countof(details), L"Logged in and active. License: %s", expiry);
            } else if (licDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s (%d days)", expiry, licDaysLeft);
            } else {
                swprintf_s(details, _countof(details), L"Logged in and active. License expires: %s", expiry);
            }
        } else if (tokenExpiry[0]) {
            if (tokenDaysLeft >= 0) {
                swprintf_s(details, _countof(details), L"Logged in and active. Account expires: %s (%d days)", tokenExpiry, tokenDaysLeft);
            } else {
                swprintf_s(details, _countof(details), L"Logged in and active. Account expires: %s", tokenExpiry);
            }
        } else if (st.maxActivations > 0) {
            swprintf_s(details, _countof(details), L"Logged in and active. Devices: %d/%d", st.currentActivations, st.maxActivations);
        } else {
            wcscpy_s(details, _countof(details), L"Logged in and active.");
        }
        wcscpy_s(out, outMax, details);
    }
}

static void BankConfigMeasureItem(MEASUREITEMSTRUCT* mis)
{
    if (!mis || mis->CtlType != ODT_LISTBOX) return;
    mis->itemHeight = 28;
}

static void BankConfigDrawOwnerItem(const DRAWITEMSTRUCT* dis, BankConfigData* data)
{
    if (!dis || !data) return;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int id = (int)dis->CtlID;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;

    if (id == IDC_BANK_LIST) {
        int bank = (int)dis->itemID;
        if (bank < 0 || bank >= NUM_BANKS) return;

        COLORREF bg = selected ? TmColor::ACCENT : TmColor::BG_CARD;
        COLORREF fg = selected ? TmColor::TEXT_PRIMARY : TmColor::TEXT_SECOND;
        TmDraw::FillRoundRect(hdc, rc, bg, TmRadius::SM);

        RECT sw = rc;
        sw.left += 8; sw.top += 6; sw.bottom -= 6; sw.right = sw.left + 16;
        TmDraw::FillRoundRect(hdc, sw, data->workingColors[bank], 6);

        RECT tr = rc;
        tr.left += 32;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        wchar_t label[64];
        if (data->workingNames[bank][0]) wcscpy_s(label, data->workingNames[bank]);
        else swprintf_s(label, L"Bank %d", bank + 1);
        DrawTextW(hdc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (focused) {
            HPEN pen = CreatePen(PS_SOLID, 1, TmColor::ACCENT_GLOW);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }
        return;
    }

    auto drawButton = [&](COLORREF fill, COLORREF text, COLORREF border, const wchar_t* label, bool accent = false) {
        RECT body = rc;
        TmDraw::FillRoundRect(hdc, body, fill, TmRadius::SM);
        TmDraw::StrokeRoundRect(hdc, body, border, TmRadius::SM, 1);
        if (selected && !accent) {
            RECT inset = body;
            InflateRect(&inset, -2, -2);
            TmDraw::StrokeRoundRect(hdc, inset, TmColor::ACCENT_GLOW, TmRadius::SM, 1);
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text);
        DrawTextW(hdc, label, -1, &body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    if (id == IDC_BANK_COLOR) {
        int bank = data->selectedBank;
        COLORREF color = (bank >= 0 && bank < NUM_BANKS) ? data->workingColors[bank] : RGB(90, 102, 242);
        drawButton(color, IsLightColor(color) ? RGB(18, 20, 28) : RGB(248, 248, 248), RGB(255, 255, 255), L"Choose Color...", true);
        return;
    }

    if (id == IDOK) {
        drawButton(TmColor::ACCENT, TmColor::TEXT_PRIMARY, TmColor::ACCENT_GLOW, L"Save");
        return;
    }

    if (id == IDCANCEL) {
        drawButton(TmColor::BG_CARD, TmColor::TEXT_SECOND, TmColor::BORDER, L"Cancel");
        return;
    }
}

static void BankConfigPaint(HWND hWnd, HDC hdc, BankConfigData* data)
{
    RECT rc = {};
    GetClientRect(hWnd, &rc);
    float dpi = data && data->ui ? data->ui->dpi : 1.0f;
    auto S = [&](int v){ return (int)(v * dpi + 0.5f); };

    TmDraw::GradientV(hdc, rc, RGB(10, 12, 18), RGB(17, 20, 30));

    RECT stripe = { 0, 0, rc.right, S(4) };
    TmDraw::GradientV(hdc, stripe, TmColor::ACCENT, TmColor::ACCENT_GLOW);

    int pad = S(16);
    int gap = S(12);
    int leftW = max(S(210), min(S(260), (rc.right - pad * 2) / 3));
    int rightX = pad + leftW + gap;
    int rightW = rc.right - rightX - pad;
    int topY = S(18);

    RECT title = { pad, topY, rc.right - pad, topY + S(28) };
    HFONT oldFont = nullptr;
    if (data && data->ui) oldFont = (HFONT)SelectObject(hdc, data->ui->fonts.heading);
    SetTextColor(hdc, RGB(226, 232, 240));
    DrawTextW(hdc, L"Bank Names & Colors", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle = { pad, topY + S(28), rc.right - pad, topY + S(52) };
    SetTextColor(hdc, RGB(148, 163, 184));
    DrawTextW(hdc, L"Rename banks and assign a custom button color.", -1, &subtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (oldFont) SelectObject(hdc, oldFont);
    SetBkMode(hdc, TRANSPARENT);

    RECT leftCard = { pad, S(78), pad + leftW, rc.bottom - S(72) };
    RECT rightCard = { rightX, S(78), rc.right - pad, rc.bottom - S(72) };
    TmDraw::FillRoundRect(hdc, leftCard, TmColor::BG_CARD, TmRadius::MD);
    TmDraw::StrokeRoundRect(hdc, leftCard, TmColor::BORDER, TmRadius::MD, 1);
    TmDraw::FillRoundRect(hdc, rightCard, TmColor::BG_CARD, TmRadius::MD);
    TmDraw::StrokeRoundRect(hdc, rightCard, TmColor::BORDER, TmRadius::MD, 1);

    RECT hdr1 = leftCard;
    hdr1.left += S(14); hdr1.top += S(10); hdr1.right -= S(14); hdr1.bottom = hdr1.top + S(22);
    SetTextColor(hdc, RGB(220, 228, 244));
    DrawTextW(hdc, L"Banks", -1, &hdr1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT hdr2 = rightCard;
    hdr2.left += S(14); hdr2.top += S(10); hdr2.right -= S(14); hdr2.bottom = hdr2.top + S(22);
    DrawTextW(hdc, L"Edit selected bank", -1, &hdr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (data && data->selectedBank >= 0 && data->selectedBank < NUM_BANKS) {
        RECT previewBox = { rightCard.left + S(16), rightCard.top + S(118), rightCard.right - S(16), rightCard.top + S(156) };
        TmDraw::FillRoundRect(hdc, previewBox, data->workingColors[data->selectedBank], TmRadius::SM);
        TmDraw::StrokeRoundRect(hdc, previewBox, TmColor::BORDER, TmRadius::SM, 1);
    }
}

static void BankConfigUpdatePreview(BankConfigData* data)
{
    if (!data || data->selectedBank < 0 || data->selectedBank >= NUM_BANKS) return;
    if (data->previewBrush) { DeleteObject(data->previewBrush); data->previewBrush = nullptr; }
    COLORREF c = data->workingColors[data->selectedBank];
    data->previewBrush = CreateSolidBrush(c);
    if (data->colorPreview) {
        wchar_t hex[32];
        swprintf_s(hex, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        SetWindowTextW(data->colorPreview, hex);
        InvalidateRect(data->colorPreview, nullptr, TRUE);
    }
}

static void BankConfigLoadSelection(BankConfigData* data, int index)
{
    if (!data || index < 0 || index >= NUM_BANKS) return;
    data->selectedBank = index;
    if (data->edit) SetWindowTextW(data->edit, data->workingNames[index][0] ? data->workingNames[index] : L"");
    if (data->list) SendMessageW(data->list, LB_SETCURSEL, index, 0);
    BankConfigUpdatePreview(data);
}

static void BankConfigCommitEdit(BankConfigData* data)
{
    if (!data || !data->edit || data->selectedBank < 0 || data->selectedBank >= NUM_BANKS) return;
    GetWindowTextW(data->edit, data->workingNames[data->selectedBank], _countof(data->workingNames[data->selectedBank]));
}

static void BankConfigRefreshListItem(BankConfigData* data)
{
    if (!data || !data->list || data->selectedBank < 0 || data->selectedBank >= NUM_BANKS) return;
    wchar_t item[64];
    swprintf_s(item, L"Bank %d", data->selectedBank + 1);
    if (data->workingNames[data->selectedBank][0]) {
        swprintf_s(item, L"%d. %ls", data->selectedBank + 1, data->workingNames[data->selectedBank]);
    }
    SendMessageW(data->list, LB_DELETESTRING, data->selectedBank, 0);
    SendMessageW(data->list, LB_INSERTSTRING, data->selectedBank, (LPARAM)item);
    SendMessageW(data->list, LB_SETCURSEL, data->selectedBank, 0);
}

static void BankConfigLayout(BankConfigData* data)
{
    if (!data || !data->hwnd) return;
    RECT rc = {};
    GetClientRect(data->hwnd, &rc);
    auto S = [&](int v){ return (int)(v * (data->ui ? data->ui->dpi : 1.0f) + 0.5f); };

    int pad = S(16);
    int gap = S(12);
    int rowH = S(34);
    int listW = max(S(210), min(S(260), (rc.right - rc.left) / 3));
    int listH = rc.bottom - S(78) - S(84);
    if (listH < S(190)) listH = S(190);
    int rightX = pad + listW + gap;
    int rightW = rc.right - rightX - pad;
    if (rightW < S(220)) rightW = S(220);

    if (data->list) MoveWindow(data->list, pad, S(110), listW, listH, TRUE);
    if (data->edit) MoveWindow(data->edit, rightX, S(138), rightW, rowH, TRUE);
    if (data->colorPreview) MoveWindow(data->colorPreview, rightX, S(198), rightW, rowH, TRUE);
    if (data->colorButton) MoveWindow(data->colorButton, rightX, S(248), rightW, rowH, TRUE);

    int footerY = rc.bottom - S(48);
    int footerW = max(S(96), (rightW - gap) / 2);
    if (GetDlgItem(data->hwnd, IDOK)) MoveWindow(GetDlgItem(data->hwnd, IDOK), rightX, footerY, footerW, rowH, TRUE);
    if (GetDlgItem(data->hwnd, IDCANCEL)) MoveWindow(GetDlgItem(data->hwnd, IDCANCEL), rightX + footerW + gap, footerY, footerW, rowH, TRUE);
}

static bool ShowBankConfigDialog(TmUIData* ui)
{
    if (!ui || !ui->hWnd) return false;
    EnsureCommonControlsInit();

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            BankConfigData* data = (BankConfigData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            static HBRUSH sDlgBgBrush = nullptr;
            static COLORREF sDlgBg = RGB(14, 16, 24);
            switch (msg) {
            case WM_NCCREATE: {
                CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
                data = (BankConfigData*)cs->lpCreateParams;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
                return TRUE;
            }
            case WM_CREATE: {
                if (!sDlgBgBrush) sDlgBgBrush = CreateSolidBrush(sDlgBg);
                RECT rc = {};
                GetClientRect(hWnd, &rc);
                int pad = 16;
                data->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                    pad, 110, 220, 240, hWnd, (HMENU)IDC_BANK_LIST, nullptr, nullptr);
                CreateWindowExW(0, L"STATIC", L"Bank name", WS_CHILD | WS_VISIBLE, 252, 138, 140, 18, hWnd, nullptr, nullptr, nullptr);
                data->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    252, 160, rc.right - 268, 30, hWnd, (HMENU)IDC_BANK_NAME, nullptr, nullptr);
                CreateWindowExW(0, L"STATIC", L"Color preview", WS_CHILD | WS_VISIBLE, 252, 198, 140, 18, hWnd, nullptr, nullptr, nullptr);
                data->colorPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                    252, 220, rc.right - 268, 30, hWnd, nullptr, nullptr, nullptr);
                data->colorButton = CreateWindowExW(0, L"BUTTON", L"Choose Color...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    252, 270, rc.right - 268, 34, hWnd, (HMENU)IDC_BANK_COLOR, nullptr, nullptr);
                CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                    252, rc.bottom - 48, 96, 34, hWnd, (HMENU)IDOK, nullptr, nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    360, rc.bottom - 48, 96, 34, hWnd, (HMENU)IDCANCEL, nullptr, nullptr);

                for (int i = 0; i < NUM_BANKS; ++i) {
                    data->workingColors[i] = data->ui->grid.bankColors[i];
                    wcscpy_s(data->workingNames[i], data->ui->grid.bankNames[i]);
                    wchar_t item[64];
                    swprintf_s(item, L"Bank %d", i + 1);
                    if (data->workingNames[i][0]) swprintf_s(item, L"%d. %ls", i + 1, data->workingNames[i]);
                    SendMessageW(data->list, LB_ADDSTRING, 0, (LPARAM)item);
                }
                SendMessageW(data->list, LB_SETCURSEL, 0, 0);
                BankConfigLoadSelection(data, 0);
                BankConfigLayout(data);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);
                BankConfigPaint(hWnd, hdc, data);
                EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_MEASUREITEM:
                if (data) BankConfigMeasureItem((MEASUREITEMSTRUCT*)lp);
                return TRUE;
            case WM_DRAWITEM:
                if (data) BankConfigDrawOwnerItem((DRAWITEMSTRUCT*)lp, data);
                return TRUE;
            case WM_SIZE:
                BankConfigLayout(data);
                return 0;
            case WM_CTLCOLORDLG:
                SetBkColor((HDC)wp, RGB(14, 16, 24));
                return (LRESULT)sDlgBgBrush;
            case WM_CTLCOLORLISTBOX:
                SetBkColor((HDC)wp, RGB(17, 20, 30));
                SetTextColor((HDC)wp, RGB(235, 240, 250));
                return (LRESULT)sDlgBgBrush;
            case WM_CTLCOLOREDIT:
                SetBkColor((HDC)wp, RGB(17, 20, 30));
                SetTextColor((HDC)wp, RGB(235, 240, 250));
                return (LRESULT)sDlgBgBrush;
            case WM_CTLCOLORSTATIC: {
                HWND child = (HWND)lp;
                SetBkMode((HDC)wp, TRANSPARENT);
                if (data && child == data->colorPreview) {
                    COLORREF c = data->workingColors[data->selectedBank];
                    SetBkColor((HDC)wp, c);
                    SetTextColor((HDC)wp, IsLightColor(c) ? RGB(16, 16, 16) : RGB(248, 248, 248));
                    return (LRESULT)(data->previewBrush ? data->previewBrush : sDlgBgBrush);
                }
                SetTextColor((HDC)wp, RGB(220, 228, 244));
                return (LRESULT)sDlgBgBrush;
            }
            case WM_COMMAND:
                if (!data) break;
                switch (LOWORD(wp)) {
                case IDC_BANK_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) {
                        BankConfigCommitEdit(data);
                        int sel = (int)SendMessageW(data->list, LB_GETCURSEL, 0, 0);
                        if (sel >= 0 && sel < NUM_BANKS) BankConfigLoadSelection(data, sel);
                    }
                    return 0;
                case IDC_BANK_NAME:
                    if (HIWORD(wp) == EN_CHANGE) {
                        BankConfigCommitEdit(data);
                        BankConfigRefreshListItem(data);
                    }
                    return 0;
                case IDC_BANK_COLOR: {
                    BankConfigCommitEdit(data);
                    int sel = data->selectedBank;
                    if (sel >= 0 && sel < NUM_BANKS) {
                        COLORREF c = data->workingColors[sel];
                        if (ChoosePanelColor(hWnd, c)) {
                            data->workingColors[sel] = c;
                            BankConfigUpdatePreview(data);
                        }
                    }
                    return 0;
                }
                case IDOK:
                    BankConfigCommitEdit(data);
                    for (int i = 0; i < NUM_BANKS; ++i) {
                        wcscpy_s(data->ui->grid.bankNames[i], data->workingNames[i]);
                        data->ui->grid.bankColors[i] = data->workingColors[i];
                    }
                    SaveBankConfig(data->ui);
                    if (data->ui->host) data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
                    InvalidateRect(data->ui->hWnd, nullptr, FALSE);
                    data->result = true;
                    data->done = true;
                    DestroyWindow(hWnd);
                    return 0;
                case IDCANCEL:
                    data->result = false;
                    data->done = true;
                    DestroyWindow(hWnd);
                    return 0;
                }
                break;
            case WM_CLOSE:
                data->result = false;
                data->done = true;
                DestroyWindow(hWnd);
                return 0;
            case WM_DESTROY:
                if (data && data->previewBrush) { DeleteObject(data->previewBrush); data->previewBrush = nullptr; }
                if (data) data->done = true;
                return 0;
            }
            return DefWindowProcW(hWnd, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TmBankConfigWnd";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }

    BankConfigData data;
    data.ui = ui;
    for (int i = 0; i < NUM_BANKS; ++i) {
        data.workingColors[i] = ui->grid.bankColors[i];
        wcscpy_s(data.workingNames[i], ui->grid.bankNames[i]);
    }

    RECT ownerRc = {};
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (GetWindowRect(ui->hWnd, &ownerRc)) {
        x = ownerRc.left + 60;
        y = ownerRc.top + 60;
    }

    HWND box = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"TmBankConfigWnd",
        L"Bank Names & Colors",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, 640, 420,
        ui->hWnd,
        nullptr,
        GetModuleHandleW(nullptr),
        &data);

    if (!box) return false;
    data.hwnd = box;
    EnableWindow(ui->hWnd, FALSE);
    ShowWindow(box, SW_SHOW);
    UpdateWindow(box);

    MSG msg = {};
    while (!data.done) {
        if (GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(box, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    EnableWindow(ui->hWnd, TRUE);
    SetForegroundWindow(ui->hWnd);
    return data.result;
}

static LRESULT CALLBACK LicenseLoginWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LicenseLoginData* data = (LicenseLoginData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    static HBRUSH sDlgBgBrush = nullptr;
    static COLORREF sDlgBg = RGB(16, 19, 30);
    static HFONT sLabelFont = nullptr;
    static HFONT sInputFont = nullptr;

    auto CreateFonts = [&](float dpi) {
        if (!sLabelFont) sLabelFont = CreateFontW((int)(16 * dpi), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!sInputFont) sInputFont = CreateFontW((int)(18 * dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        data = (LicenseLoginData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    }
    case WM_CREATE: {
        if (!sDlgBgBrush) sDlgBgBrush = CreateSolidBrush(sDlgBg);
        RECT rc = {};
        GetClientRect(hWnd, &rc);
        int pad = 16;
        int rowH = 24;
        int gap = 8;
        int innerW = rc.right - pad * 2;
        int logoAreaH = 80;

        CreateWindowExW(0, L"STATIC", L"Email", WS_CHILD | WS_VISIBLE,
            pad, logoAreaH, innerW, 18, hWnd, nullptr, nullptr, nullptr);
        data->editEmail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            pad, logoAreaH + 20, innerW, 26, hWnd, (HMENU)IDC_LICENSE_EMAIL, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"Password", WS_CHILD | WS_VISIBLE,
            pad, logoAreaH + 58, innerW, 18, hWnd, nullptr, nullptr, nullptr);
        data->editPassword = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
            pad, logoAreaH + 78, innerW, 26, hWnd, (HMENU)IDC_LICENSE_PASSWORD, nullptr, nullptr);

        data->chkSave = CreateWindowExW(0, L"BUTTON", L"Save password in Windows Credential Manager",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            pad, logoAreaH + 116, innerW, rowH, hWnd, (HMENU)IDC_LICENSE_SAVE, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Login", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            rc.right - pad * 2 - 182, rc.bottom - 44, 84, 30, hWnd, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            rc.right - pad * 2 - 92, rc.bottom - 44, 84, 30, hWnd, (HMENU)IDCANCEL, nullptr, nullptr);

        LicenseState st = {};
        TmLicense::Init(&st);
        if (data->ui && data->ui->host && data->ui->host->HostGetLicenseState(&st)) {
            if (st.userEmail[0]) {
                SetWindowTextW(data->editEmail, st.userEmail);
            }
            SendMessageW(data->chkSave, BM_SETCHECK, st.savePassword ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        return 0;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        SetBkColor((HDC)wp, sDlgBg);
        SetTextColor((HDC)wp, RGB(235, 240, 250));
        return (LRESULT)sDlgBgBrush;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(12, 14, 22));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        HINSTANCE hInst = GetModuleHandleW(nullptr);
        static Gdiplus::Image* sLogoImage = nullptr;
        if (!sLogoImage && hInst) {
            sLogoImage = LoadPngFromResource(hInst, 102);
        }
        if (sLogoImage && sLogoImage->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            int logoW = sLogoImage->GetWidth();
            int logoH = sLogoImage->GetHeight();
            int maxLogoH = 60;
            float scale = (logoH > maxLogoH) ? (float)maxLogoH / logoH : 1.0f;
            int drawW = (int)(logoW * scale);
            int drawH = (int)(logoH * scale);
            int drawX = (rc.right - drawW) / 2;
            int drawY = 12;
            graphics.DrawImage(sLogoImage, drawX, drawY, drawW, drawH);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (!data) break;
        switch (LOWORD(wp)) {
        case IDOK: {
            wchar_t email[256] = {};
            wchar_t password[256] = {};
            GetWindowTextW(data->editEmail, email, _countof(email));
            GetWindowTextW(data->editPassword, password, _countof(password));
            bool save = (SendMessageW(data->chkSave, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (!data->ui || !data->ui->host || !data->ui->host->HostLicenseLogin(email, password, save)) {
                LicenseState st = {};
                TmLicense::Init(&st);
                if (data->ui && data->ui->host) data->ui->host->HostGetLicenseState(&st);
                wchar_t msg[256] = L"License login failed.";
                if (st.message[0]) MultiByteToWideChar(CP_UTF8, 0, st.message, -1, msg, _countof(msg));
                MessageBoxW(hWnd, msg, L"TellyMedia License", MB_OK | MB_ICONWARNING);
                return 0;
            }
            data->result = true;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        }
        case IDCANCEL:
            data->result = false;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (data) {
            data->result = false;
            data->done = true;
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (data) data->done = true;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static bool ShowLicenseLoginDialog(TmUIData* ui)
{
    if (!ui || !ui->hWnd) return false;
    EnsureCommonControlsInit();

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LicenseLoginWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TmLicenseLoginWnd";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }

    LicenseLoginData data = {};
    data.ui = ui;

    RECT ownerRc = {};
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (GetWindowRect(ui->hWnd, &ownerRc)) {
        x = ownerRc.left + 80;
        y = ownerRc.top + 80;
    }

    HWND box = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"TmLicenseLoginWnd",
        L"TellyMedia License Login",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, 520, 240,
        ui->hWnd,
        nullptr,
        GetModuleHandleW(nullptr),
        &data);

    if (!box) return false;
    data.hwnd = box;
    EnableWindow(ui->hWnd, FALSE);
    ShowWindow(box, SW_SHOW);
    UpdateWindow(box);

    MSG msg = {};
    while (!data.done) {
        if (GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(box, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    EnableWindow(ui->hWnd, TRUE);
    SetForegroundWindow(ui->hWnd);
    return data.result;
}

static LRESULT CALLBACK TextPanelEditorWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TextPanelEditorData* data = (TextPanelEditorData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    static HBRUSH sDlgBgBrush = nullptr;
    static HBRUSH sEditBgBrush = nullptr;
    static COLORREF sDlgBg = RGB(14, 16, 24);
    static COLORREF sEditBg = RGB(18, 20, 30);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        data = (TextPanelEditorData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    }
    case WM_CREATE: {
        if (!sDlgBgBrush) sDlgBgBrush = CreateSolidBrush(sDlgBg);
        if (!sEditBgBrush) sEditBgBrush = CreateSolidBrush(sEditBg);

        RECT rcClient = { 0, 0, 560, 520 };
        int pad = 12;
        int rowH = 26;
        int btnW = 108;
        int gap = 8;
        int innerW = rcClient.right - pad * 2;

        CreateWindowExW(0, L"STATIC", L"Text:", WS_CHILD | WS_VISIBLE,
            pad, pad, innerW, 18, hWnd, nullptr, nullptr, nullptr);

        data->editText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            pad, pad + 20, innerW, 120, hWnd, (HMENU)1000, nullptr, nullptr);

        data->lblFont = CreateWindowExW(0, L"STATIC", L"Font:", WS_CHILD | WS_VISIBLE,
            pad, 160, innerW, 18, hWnd, nullptr, nullptr, nullptr);
        data->lblSizeValue = CreateWindowExW(0, L"STATIC", L"Size: 32", WS_CHILD | WS_VISIBLE,
            pad, 182, innerW, 18, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Font...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad, 208, btnW, rowH, hWnd, (HMENU)2001, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Text RGBA", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + btnW + gap, 208, btnW, rowH, hWnd, (HMENU)2002, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Bg RGBA", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + (btnW + gap) * 2, 208, btnW, rowH, hWnd, (HMENU)2003, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Text Gradient RGBA", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad, 244, btnW, rowH, hWnd, (HMENU)2006, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Bg Gradient RGBA", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + btnW + gap, 244, btnW, rowH, hWnd, (HMENU)2007, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + (btnW + gap) * 2, 244, 36, rowH, hWnd, (HMENU)2004, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + (btnW + gap) * 2 + 44, 244, 36, rowH, hWnd, (HMENU)2005, nullptr, nullptr);

        data->lblSizeHint = CreateWindowExW(0, L"STATIC", L"Use +/- to adjust size", WS_CHILD | WS_VISIBLE,
            pad + (btnW + gap) * 2 + 88, 248, 240, 18, hWnd, nullptr, nullptr, nullptr);

        // Outline controls
        CreateWindowExW(0, L"STATIC", L"Outline:", WS_CHILD | WS_VISIBLE,
            pad, 272, innerW, 18, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Outline RGB", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad, 292, btnW, rowH, hWnd, (HMENU)2008, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + btnW + gap, 292, 36, rowH, hWnd, (HMENU)2009, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            pad + btnW + gap + 44, 292, 36, rowH, hWnd, (HMENU)2010, nullptr, nullptr);

        data->lblOutlineValue = CreateWindowExW(0, L"STATIC", L"Width: 0", WS_CHILD | WS_VISIBLE,
            pad + btnW + gap + 88, 292, 240, 18, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"Animation Mode:", WS_CHILD | WS_VISIBLE,
            pad, 328, innerW, 18, hWnd, nullptr, nullptr, nullptr);

        data->cmbAnimMode = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            pad, 348, innerW, 180, hWnd, (HMENU)2011, nullptr, nullptr);
        if (data->cmbAnimMode) {
            SendMessageW(data->cmbAnimMode, CB_ADDSTRING, 0, (LPARAM)L"Static");
            SendMessageW(data->cmbAnimMode, CB_ADDSTRING, 0, (LPARAM)L"Scroll Left");
            SendMessageW(data->cmbAnimMode, CB_ADDSTRING, 0, (LPARAM)L"Scroll Right");
            SendMessageW(data->cmbAnimMode, CB_ADDSTRING, 0, (LPARAM)L"Bounce");
            SendMessageW(data->cmbAnimMode, CB_ADDSTRING, 0, (LPARAM)L"Zoom In/Out");
            SendMessageW(data->cmbAnimMode, CB_ADDSTRING, 0, (LPARAM)L"Typewriter");
        }

        data->lblBounceSpeed = CreateWindowExW(0, L"STATIC", L"Bounce Speed:", WS_CHILD | WS_VISIBLE,
            pad, 376, innerW, 18, hWnd, nullptr, nullptr, nullptr);

        data->cmbBounceSpeed = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            pad, 396, innerW, 150, hWnd, (HMENU)2012, nullptr, nullptr);
        if (data->cmbBounceSpeed) {
            for (int i = 1; i <= 30; ++i) {
                wchar_t buf[8];
                _itow_s(i, buf, 10);
                SendMessageW(data->cmbBounceSpeed, CB_ADDSTRING, 0, (LPARAM)buf);
            }
            SendMessageW(data->cmbBounceSpeed, CB_SETCURSEL, 14, 0); // Default to 15 (middle)
        }

        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            rcClient.right - pad * 2 - 180, 440, 80, rowH, hWnd, (HMENU)IDOK, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            rcClient.right - pad * 2 - 92, 440, 80, rowH, hWnd, (HMENU)IDCANCEL, nullptr, nullptr);

        if (data) {
            SetWindowTextW(data->editText, data->working.textContent);
            UpdateTextPanelEditorLabels(data);
            TextPanelEditorLayoutControls(data);
            // Initialize bounce speed control visibility
            if (data->lblBounceSpeed && data->cmbBounceSpeed) {
                ShowWindow(data->lblBounceSpeed, (data->working.textAnimMode == 3) ? SW_SHOW : SW_HIDE);
                ShowWindow(data->cmbBounceSpeed, (data->working.textAnimMode == 3) ? SW_SHOW : SW_HIDE);
            }
            SendMessageW(data->editText, EM_SETSEL, 0, -1);
            SetFocus(data->editText);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORDLG:
        SetBkColor((HDC)wp, sDlgBg);
        return (LRESULT)sDlgBgBrush;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(220, 228, 244));
        return (LRESULT)sDlgBgBrush;
    case WM_CTLCOLOREDIT:
        SetBkMode((HDC)wp, OPAQUE);
        SetBkColor((HDC)wp, sEditBg);
        SetTextColor((HDC)wp, RGB(240, 244, 252));
        return (LRESULT)sEditBgBrush;
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, sDlgBg);
        SetTextColor((HDC)wp, RGB(235, 240, 250));
        return (LRESULT)sDlgBgBrush;
    case WM_SIZE:
        if (data) {
            TextPanelEditorLayoutControls(data);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        // Background gradient
        TmDraw::GradientV(hdc, rc, RGB(7, 9, 14), RGB(18, 20, 30));

        RECT topGlow = { 0, 0, rc.right, 72 };
        TmDraw::GradientV(hdc, topGlow, RGB(30, 40, 62), RGB(7, 9, 14));

        // Subtle vignette panel behind controls
        int pad = 18;
        RECT shadow = { pad + 8, pad + 10, rc.right - pad + 8, rc.bottom - pad + 10 };
        RECT card = { pad, pad, rc.right - pad, rc.bottom - pad };
        TmDraw::FillRoundRect(hdc, shadow, RGB(4, 6, 10), 16);
        TmDraw::Card(hdc, card, RGB(20, 23, 34), 16, true);

        RECT accent = { card.left + 1, card.top + 1, card.right - 1, card.top + 4 };
        TmDraw::FillRoundRect(hdc, accent, RGB(88, 121, 214), 2);

        RECT titleRc = { card.left + 18, card.top + 14, card.right - 18, card.top + 42 };
        TmDraw::TextLeft(hdc, titleRc, L"Edit Text & Style", TmColor::TEXT_PRIMARY,
            data && data->ui ? data->ui->fonts.heading : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

        RECT subRc = { card.left + 18, card.top + 38, card.right - 18, card.top + 58 };
        TmDraw::TextLeft(hdc, subRc, L"Right-click editing with live preview", TmColor::TEXT_THIRD,
            data && data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

        RECT hintRc = { card.left + 18, card.top + 64, card.right - 18, card.top + 84 };
        TmDraw::TextLeft(hdc, hintRc, L"Premium dark theme", RGB(130, 150, 190),
            data && data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (!data) break;
        switch (LOWORD(wp)) {
        case IDOK:
            ReadTextPanelEditorText(data);
            if (data->ui && data->panelIdx >= 0 && data->panelIdx < data->ui->numLayoutPanels) {
                data->ui->layoutPanels[data->panelIdx] = data->working;
                if (data->ui->host) data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
                TmUI::SaveState(data->ui);
                InvalidateRect(data->ui->hWnd, nullptr, FALSE);
            }
            data->result = true;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        case IDCANCEL:
            data->working = data->original;
            if (data->ui && data->panelIdx >= 0 && data->panelIdx < data->ui->numLayoutPanels) {
                data->ui->layoutPanels[data->panelIdx] = data->original;
                if (data->ui->host) data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
                InvalidateRect(data->ui->hWnd, nullptr, FALSE);
            }
            data->result = false;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        case 2001: // Font...
            ReadTextPanelEditorText(data);
            if (ChoosePanelFont(hWnd, data->working)) {
                SetWindowTextW(data->editText, data->working.textContent);
                UpdateTextPanelEditorLabels(data);
                ApplyTextPanelEditorPreview(data);
            }
            return 0;
        case 2002: // Text Color
            ReadTextPanelEditorText(data);
            if (ChoosePanelColorRGBA(hWnd, data->working.textColor, data->working.textAlpha)) {
                data->working.textAlpha2 = data->working.textAlpha;
            }
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2006: // Text Gradient
            ReadTextPanelEditorText(data);
            ChoosePanelColorRGBA(hWnd, data->working.textColor2, data->working.textAlpha2);
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2003: // Bg Color
            ReadTextPanelEditorText(data);
            {
                BYTE bgAlpha = (BYTE)max(0, min(data->working.bgOpacity, 255));
                if (ChoosePanelColorRGBA(hWnd, data->working.bgColor, bgAlpha)) {
                    data->working.bgOpacity = bgAlpha;
                    data->working.hasBgColor = true;
                }
            }
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2007: // Bg Gradient
            ReadTextPanelEditorText(data);
            {
                BYTE bgAlpha = (BYTE)max(0, min(data->working.bgOpacity, 255));
                if (ChoosePanelColorRGBA(hWnd, data->working.bgColor2, bgAlpha)) {
                    data->working.bgOpacity = bgAlpha;
                    data->working.hasBgColor = true;
                    data->working.bgStyle = 1;
                }
            }
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2004: // size -
            ReadTextPanelEditorText(data);
            if (data->working.fontSize > 8) data->working.fontSize -= 2;
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2005: // size +
            ReadTextPanelEditorText(data);
            if (data->working.fontSize < 200) data->working.fontSize += 2;
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2008: // Outline Color
            ReadTextPanelEditorText(data);
            {
                static DWORD customColors[16] = {
                    0xFFFFFF, 0x000000, 0xFF0000, 0x00FF00, 0x0000FF,
                    0xFFFF00, 0xFF00FF, 0x00FFFF, 0x808080, 0xC0C0C0,
                    0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080
                };
                CHOOSECOLORW cc = {};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hWnd;
                cc.rgbResult = data->working.outlineColor;
                cc.lpCustColors = customColors;
                cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (ChooseColorW(&cc)) {
                    data->working.outlineColor = cc.rgbResult;
                }
            }
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2009: // outline width -
            ReadTextPanelEditorText(data);
            if (data->working.outlineWidth > 0) data->working.outlineWidth -= 1;
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2010: // outline width +
            ReadTextPanelEditorText(data);
            if (data->working.outlineWidth < 20) data->working.outlineWidth += 1;
            UpdateTextPanelEditorLabels(data);
            ApplyTextPanelEditorPreview(data);
            return 0;
        case 2011: // animation mode combo
            if (HIWORD(wp) == CBN_SELCHANGE && data->cmbAnimMode) {
                data->working.textAnimMode = (int)SendMessageW(data->cmbAnimMode, CB_GETCURSEL, 0, 0);
                // Show/hide bounce speed control based on selection
                if (data->lblBounceSpeed && data->cmbBounceSpeed) {
                    ShowWindow(data->lblBounceSpeed, (data->working.textAnimMode == 3) ? SW_SHOW : SW_HIDE);
                    ShowWindow(data->cmbBounceSpeed, (data->working.textAnimMode == 3) ? SW_SHOW : SW_HIDE);
                }
                UpdateTextPanelEditorLabels(data);
                ApplyTextPanelEditorPreview(data);
            }
            return 0;
        case 2012: // bounce speed combo
            if (HIWORD(wp) == CBN_SELCHANGE && data->cmbBounceSpeed) {
                int speedSel = (int)SendMessageW(data->cmbBounceSpeed, CB_GETCURSEL, 0, 0);
                // Map 0-29 to speed range 30-900 (1=very slow, 30=very fast)
                data->working.scrollSpeed = 30.0f * (speedSel + 1);
                ApplyTextPanelEditorPreview(data);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        if (data) {
            data->working = data->original;
            if (data->ui && data->panelIdx >= 0 && data->panelIdx < data->ui->numLayoutPanels) {
                data->ui->layoutPanels[data->panelIdx] = data->original;
                if (data->ui->host) data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
                InvalidateRect(data->ui->hWnd, nullptr, FALSE);
            }
            data->done = true;
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (data) data->done = true;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static bool ShowTextPanelEditor(TmUIData* ui, int panelIdx)
{
    if (!ui || panelIdx < 0 || panelIdx >= ui->numLayoutPanels) return false;

    static bool registered = false;
    static const wchar_t* kClassName = L"TellyMediaTextPanelEditor";
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TextPanelEditorWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        registered = true;
    }

    TextPanelEditorData data = {};
    data.ui = ui;
    data.panelIdx = panelIdx;
    data.original = ui->layoutPanels[panelIdx];
    data.working = ui->layoutPanels[panelIdx];

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    RECT rc = { 0, 0, 720, 680 };
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    RECT ownerRc = {};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (ui->hWnd && GetWindowRect(ui->hWnd, &ownerRc)) {
        x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
        y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - height) / 2;
    }

    HWND box = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        L"Edit Text & Style",
        style,
        x, y, width, height,
        ui->hWnd,
        nullptr,
        GetModuleHandleW(nullptr),
        &data);

    if (!box) return false;

    data.hwnd = box;
    if (ui->hWnd) EnableWindow(ui->hWnd, FALSE);
    ShowWindow(box, SW_SHOW);
    UpdateWindow(box);

    MSG msg = {};
    while (!data.done) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!data.done) WaitMessage();
    }

    if (ui->hWnd) {
        EnableWindow(ui->hWnd, TRUE);
        SetForegroundWindow(ui->hWnd);
    }

    return data.result;
}

// Song panel editor window (modal)
struct SongPanelEditorData {
    TmUIData* ui;
    int panelIdx;
    OverlayPanel original;
    OverlayPanel working;
    HWND hwnd;
    HWND lblFont;
    HWND lblSizeValue;
    HWND lblSongFontValue;
    HWND chkRainbow;
    HWND cmbAnimMode;
    HWND scrScrollSpeed;
    HWND cmbTextAlign;
    int scrollPos;
    int contentHeight;
    int hoverControl;
    int pressedControl;
    bool tracking;
    bool scrollDragging;
    int scrollDragOffset;
    bool result;
    bool done;
};

static void ApplySongPanelEditorPreview(SongPanelEditorData* data);
static int SongPanelEditorScrollBarW(const SongPanelEditorData* data);

struct SongPanelEditorLayout {
    RECT client;
    RECT card;
    bool compact;
    int pad;
    int gap;
    int colGap;
    int rowH;
    int smallH;
    int sbw;
    int contentLeft;
    int contentRight;
    int contentW;
    int leftX;
    int rightX;
    int colW;
    int headingY;
    int songY;
    int animY;
    int speedY;
    int alignY;
    int footerY;
};

static RECT SongPanelEditorCardRect(const SongPanelEditorData* data)
{
    RECT rc = {};
    if (!data || !data->hwnd) return rc;
    GetClientRect(data->hwnd, &rc);
    int pad = (int)(18 * (data->ui ? data->ui->dpi : 1.0f) + 0.5f);
    rc.left += pad;
    rc.top += pad;
    rc.right -= pad;
    rc.bottom -= pad;
    return rc;
}

static SongPanelEditorLayout SongPanelEditorCalcLayout(const SongPanelEditorData* data)
{
    SongPanelEditorLayout layout = {};
    if (!data || !data->hwnd) return layout;

    GetClientRect(data->hwnd, &layout.client);
    layout.card = SongPanelEditorCardRect(data);
    auto S = [&](int v){ return (int)(v * (data->ui ? data->ui->dpi : 1.0f) + 0.5f); };

    layout.pad = S(18);
    layout.gap = S(10);
    layout.colGap = S(12);
    layout.rowH = S(30);
    layout.smallH = S(28);
    layout.sbw = SongPanelEditorScrollBarW(data);
    layout.compact = (layout.client.right - layout.client.left) < S(760) || (layout.client.bottom - layout.client.top) < S(560);
    layout.contentLeft = layout.card.left + S(18);
    layout.contentRight = layout.card.right - S(18) - layout.sbw - S(10);
    layout.contentW = max(S(220), layout.contentRight - layout.contentLeft);
    layout.leftX = layout.contentLeft;
    layout.rightX = layout.compact ? layout.leftX : layout.leftX + (layout.contentW - layout.colGap) / 2 + layout.colGap;
    layout.colW = layout.compact ? layout.contentW : (layout.contentW - layout.colGap) / 2;
    layout.headingY = layout.card.top + (layout.compact ? S(92) : S(104));
    layout.songY = layout.compact ? layout.headingY + S(150) : layout.headingY;
    layout.animY = layout.compact ? layout.songY + S(176) : layout.headingY + S(164);
    layout.speedY = layout.compact ? layout.animY + S(152) : layout.animY;
    layout.alignY = layout.compact ? layout.speedY + S(148) : layout.animY + S(92);
    layout.footerY = layout.compact ? layout.alignY + S(96) : layout.alignY + S(82);
    return layout;
}

static bool ChooseFontFaceOnly(HWND owner, wchar_t* fontName, size_t fontNameCount)
{
    if (!fontName || fontNameCount == 0) return false;

    LOGFONTW lf = {};
    wcscpy_s(lf.lfFaceName, fontName[0] ? fontName : L"Segoe UI");

    CHOOSEFONTW cf = { sizeof(cf) };
    cf.hwndOwner = owner;
    cf.lpLogFont = &lf;
    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;

    if (!ChooseFontW(&cf)) return false;

    wcscpy_s(fontName, fontNameCount, lf.lfFaceName);
    return true;
}

static void SongPanelEditorUpdateScrollBar(SongPanelEditorData* data)
{
    if (!data || !data->hwnd) return;

    RECT rc = {};
    GetClientRect(data->hwnd, &rc);
    int clientH = max(1, rc.bottom - rc.top);
    int maxScroll = max(0, data->contentHeight - clientH);
    if (data->scrollPos > maxScroll) data->scrollPos = maxScroll;
    if (data->scrollPos < 0) data->scrollPos = 0;
}

static void SongPanelEditorSetScroll(SongPanelEditorData* data, int newPos)
{
    if (!data || !data->hwnd) return;

    RECT rc = {};
    GetClientRect(data->hwnd, &rc);
    int clientH = max(1, rc.bottom - rc.top);
    int maxScroll = max(0, data->contentHeight - clientH);
    if (newPos < 0) newPos = 0;
    if (newPos > maxScroll) newPos = maxScroll;
    data->scrollPos = newPos;
    SongPanelEditorUpdateScrollBar(data);
    InvalidateRect(data->hwnd, nullptr, FALSE);
}

static int SongPanelEditorScrollBarW(const SongPanelEditorData* data)
{
    return data && data->ui ? ScrollBarW(data->ui) : 12;
}

static float SongPanelSpeedFromLevel(int level)
{
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    return 50.0f + (float)(level - 1) * 50.0f;
}

static int SongPanelSpeedLevelFromValue(float speed)
{
    int level = (int)((speed - 50.0f + 25.0f) / 50.0f) + 1;
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    return level;
}

static void SetSongPanelSpeedLevel(SongPanelEditorData* data, int level)
{
    if (!data) return;
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    data->working.scrollSpeed = SongPanelSpeedFromLevel(level);
    if (data->hwnd) {
        CheckRadioButton(data->hwnd, 3010, 3019, 3010 + (level - 1));
    }
    ApplySongPanelEditorPreview(data);
}

static void ApplySongPanelEditorPreview(SongPanelEditorData* data)
{
    if (!data || !data->ui || data->panelIdx < 0 || data->panelIdx >= data->ui->numLayoutPanels) return;
    data->ui->layoutPanels[data->panelIdx] = data->working;
    if (data->ui->host) {
        data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
    }
    InvalidateRect(data->ui->hWnd, nullptr, FALSE);
}

static void UpdateSongPanelEditorLabels(SongPanelEditorData* data)
{
    if (!data || !data->hwnd) return;
    wchar_t fontLabel[128];
    wchar_t songFontLabel[128];
    wchar_t sizeLabel[64];
    swprintf_s(fontLabel, L"Heading Font: %ls", data->working.fontName[0] ? data->working.fontName : L"Segoe UI");
    swprintf_s(songFontLabel, L"Song Font: %ls", data->working.songFontName[0] ? data->working.songFontName : L"Segoe UI");
    swprintf_s(sizeLabel, L"Heading Size: %d", data->working.fontSize);
    if (data->lblFont) SetWindowTextW(data->lblFont, fontLabel);
    if (data->lblSizeValue) SetWindowTextW(data->lblSizeValue, sizeLabel);
    if (data->lblSongFontValue) SetWindowTextW(data->lblSongFontValue, songFontLabel);
}

static const wchar_t* GetAnimModeName(int mode)
{
    switch (mode) {
        case 0: return L"Static";
        case 1: return L"Pulse";
        case 2: return L"Wave";
        case 3: return L"Glitch";
        case 4: return L"Neon";
        case 5: return L"Bounce";
        case 6: return L"Scroll";
        default: return L"Static";
    }
}

static void SongPanelEditorBuildButtons(SongPanelEditorData* data, std::vector<SbButton>& out)
{
    out.clear();
    if (!data || !data->hwnd) return;

    SongPanelEditorLayout lay = SongPanelEditorCalcLayout(data);
    auto S = [&](int v){ return (int)(v * data->ui->dpi + 0.5f); };
    int y = lay.headingY;
    int smallBtnW = S(52);

    // Heading controls
    if (lay.compact) {
        out.push_back({ { lay.leftX, y + S(12), lay.leftX + lay.colW, y + S(12) + lay.rowH }, 3001, L"Choose Font...", false, false });
        out.push_back({ { lay.leftX, y + S(48), lay.leftX + lay.colW, y + S(48) + lay.rowH }, 3002, L"Heading RGBA", false, false });
        out.push_back({ { lay.leftX, y + S(84), lay.leftX + lay.colW, y + S(84) + lay.rowH }, 3401, L"Heading Gradient RGBA", data->working.headingTextColor2 != data->working.headingTextColor, false });
        out.push_back({ { lay.leftX, y + S(120), lay.leftX + lay.colW, y + S(120) + lay.rowH }, 3003, L"Background RGBA", false, false });
        out.push_back({ { lay.leftX, y + S(156), lay.leftX + lay.colW, y + S(156) + lay.rowH }, 3403, L"Background Gradient RGBA", data->working.bgStyle == 1, false });
        int half = (lay.colW - lay.gap) / 2;
        out.push_back({ { lay.leftX, y + S(192), lay.leftX + half, y + S(192) + lay.rowH }, 3004, L"-", false, false });
        out.push_back({ { lay.leftX + half + lay.gap, y + S(192), lay.leftX + lay.colW, y + S(192) + lay.rowH }, 3005, L"+", false, false });
        out.push_back({ { lay.leftX, y + S(228), lay.leftX + lay.colW, y + S(228) + lay.rowH }, 3006, L"Rainbow Color Effect", data->working.rainbowColor, true });
    } else {
        out.push_back({ { lay.leftX, y + S(12), lay.leftX + S(124), y + S(12) + lay.rowH }, 3001, L"Choose Font...", false, false });
        out.push_back({ { lay.leftX + S(132), y + S(12), lay.leftX + S(132) + S(108), y + S(12) + lay.rowH }, 3002, L"Heading RGBA", false, false });
        out.push_back({ { lay.leftX + S(248), y + S(12), lay.leftX + S(248) + S(132), y + S(12) + lay.rowH }, 3401, L"Heading Gradient RGBA", data->working.headingTextColor2 != data->working.headingTextColor, false });
        out.push_back({ { lay.leftX, y + S(50), lay.leftX + S(108), y + S(50) + lay.rowH }, 3003, L"Background RGBA", false, false });
        out.push_back({ { lay.leftX + S(116), y + S(50), lay.leftX + S(116) + S(132), y + S(50) + lay.rowH }, 3403, L"Background Gradient RGBA", data->working.bgStyle == 1, false });
        out.push_back({ { lay.leftX + S(256), y + S(50), lay.leftX + S(256) + smallBtnW, y + S(50) + lay.rowH }, 3004, L"-", false, false });
        out.push_back({ { lay.leftX + S(314), y + S(50), lay.leftX + S(314) + smallBtnW, y + S(50) + lay.rowH }, 3005, L"+", false, false });
        out.push_back({ { lay.leftX, y + S(88), lay.leftX + lay.colW, y + S(88) + lay.rowH }, 3006, L"Rainbow Color Effect", data->working.rainbowColor, true });
    }

    // Song controls
    if (lay.compact) {
        out.push_back({ { lay.rightX, lay.songY + S(12), lay.rightX + lay.colW, lay.songY + S(12) + lay.rowH }, 3020, L"Song Font...", false, false });
        out.push_back({ { lay.rightX, lay.songY + S(48), lay.rightX + lay.colW, lay.songY + S(48) + lay.rowH }, 3021, L"Song RGBA", false, false });
        out.push_back({ { lay.rightX, lay.songY + S(84), lay.rightX + lay.colW, lay.songY + S(84) + lay.rowH }, 3402, L"Song Gradient RGBA", data->working.songTextColor2 != data->working.songTextColor, false });
    } else {
        out.push_back({ { lay.rightX, y + S(12), lay.rightX + S(128), y + S(12) + lay.rowH }, 3020, L"Song Font...", false, false });
        out.push_back({ { lay.rightX + S(136), y + S(12), lay.rightX + S(136) + S(116), y + S(12) + lay.rowH }, 3021, L"Song RGBA", false, false });
        out.push_back({ { lay.rightX + S(260), y + S(12), lay.rightX + S(260) + S(128), y + S(12) + lay.rowH }, 3402, L"Song Gradient RGBA", data->working.songTextColor2 != data->working.songTextColor, false });
    }

    // Animation section
    y = lay.animY;
    out.push_back({ { lay.leftX, y, lay.leftX + lay.colW, y + S(20) }, -1, L"Animation", false, false });

    const wchar_t* modeLabels[4] = { L"Static", L"Scroll Left", L"Scroll Right", L"Scroll Bounce" };
    if (lay.compact) {
        int modeW = (lay.colW - lay.gap) / 2;
        for (int i = 0; i < 4; ++i) {
            int row = i / 2;
            int col = i % 2;
            int bx = lay.leftX + col * (modeW + lay.gap);
            int by = y + S(26) + row * (lay.rowH + lay.gap);
            out.push_back({ { bx, by, bx + modeW, by + lay.rowH }, 3100 + i, modeLabels[i], data->working.songAnimMode == i, false });
        }
    } else {
        int modeW = (lay.colW - lay.gap * 3) / 4;
        for (int i = 0; i < 4; ++i) {
            out.push_back({ { lay.leftX + i * (modeW + lay.gap), y + S(26), lay.leftX + i * (modeW + lay.gap) + modeW, y + S(26) + lay.rowH },
                3100 + i, modeLabels[i], data->working.songAnimMode == i, false });
        }
    }

    out.push_back({ { lay.rightX, lay.speedY, lay.rightX + lay.colW, lay.speedY + S(20) }, -1, L"Scroll Speed (1 = Slow, 10 = Fast)", false, false });
    if (lay.compact) {
        int speedW = (lay.colW - lay.gap) / 2;
        int speedY = lay.speedY + S(26);
        for (int i = 0; i < 10; ++i) {
            int row = i / 2;
            int col = i % 2;
            int bx = lay.rightX + col * (speedW + lay.gap);
            int by = speedY + row * (lay.smallH + lay.gap);
            out.push_back({ { bx, by, bx + speedW, by + lay.smallH }, 3010 + i, L"", SongPanelSpeedLevelFromValue(data->working.scrollSpeed) == (i + 1), false });
        }
    } else {
        int speedW = (lay.colW - lay.gap * 4) / 5;
        int speedY = lay.speedY + S(26);
        for (int i = 0; i < 10; ++i) {
            int row = i / 5;
            int col = i % 5;
            int bx = lay.rightX + col * (speedW + lay.gap);
            int by = speedY + row * (lay.smallH + lay.gap);
            out.push_back({ { bx, by, bx + speedW, by + lay.smallH }, 3010 + i, L"", SongPanelSpeedLevelFromValue(data->working.scrollSpeed) == (i + 1), false });
        }
    }

    // Alignment section
    y = lay.alignY;
    out.push_back({ { lay.leftX, y, lay.leftX + lay.colW, y + S(20) }, -1, L"Alignment", false, false });
    const wchar_t* alignLabels[3] = { L"Left", L"Center", L"Right" };
    if (lay.compact) {
        int alignW = (lay.colW - lay.gap) / 2;
        for (int i = 0; i < 3; ++i) {
            int row = i / 2;
            int col = i % 2;
            int bx = lay.leftX + col * (alignW + lay.gap);
            int by = y + S(26) + row * (lay.rowH + lay.gap);
            int bw = (i == 2 && col == 0) ? lay.colW : alignW;
            out.push_back({ { bx, by, bx + bw, by + lay.rowH }, 3200 + i, alignLabels[i], data->working.textAlign == i, false });
        }
    } else {
        int alignW = (lay.colW - lay.gap * 2) / 3;
        for (int i = 0; i < 3; ++i) {
            out.push_back({ { lay.leftX + i * (alignW + lay.gap), y + S(26), lay.leftX + i * (alignW + lay.gap) + alignW, y + S(26) + lay.rowH },
                3200 + i, alignLabels[i], data->working.textAlign == i, false });
        }
    }

    // Footer actions
    y = lay.footerY;
    if (lay.compact) {
        int footerW = (lay.colW - lay.gap) / 2;
        out.push_back({ { lay.leftX, y, lay.leftX + footerW, y + lay.rowH }, 3300, L"OK", false, false });
        out.push_back({ { lay.leftX + footerW + lay.gap, y, lay.leftX + lay.colW, y + lay.rowH }, 3301, L"Cancel", false, false });
    } else {
        out.push_back({ { lay.leftX, y, lay.leftX + S(140), y + lay.rowH }, 3300, L"OK", false, false });
        out.push_back({ { lay.leftX + S(152), y, lay.leftX + S(292), y + lay.rowH }, 3301, L"Cancel", false, false });
    }

    data->contentHeight = y + S(56);
}
static RECT SongPanelEditorScrollTrackRect(const SongPanelEditorData* data)
{
    RECT card = SongPanelEditorCardRect(data);
    int sbw = SongPanelEditorScrollBarW(data);
    RECT track = { card.right - sbw, card.top + 78, card.right, card.bottom - 58 };
    return track;
}

static RECT SongPanelEditorScrollThumbRect(const SongPanelEditorData* data)
{
    RECT track = SongPanelEditorScrollTrackRect(data);
    RECT card = SongPanelEditorCardRect(data);
    int viewH = max(1, card.bottom - card.top - 136);
    int contentH = max(1, data->contentHeight);
    int thumbH = max((int)(30 * (data->ui ? data->ui->dpi : 1.0f)), viewH * viewH / contentH);
    int maxScroll = max(0, contentH - viewH);
    int thumbY = track.top + (maxScroll > 0 ? data->scrollPos * (track.bottom - track.top - thumbH) / maxScroll : 0);
    RECT thumb = { track.left + 2, thumbY, track.right - 2, thumbY + thumbH };
    return thumb;
}

static bool SongPanelEditorPointInScrollableCard(const SongPanelEditorData* data, int x, int y)
{
    RECT card = SongPanelEditorCardRect(data);
    return PtInRect(&card, { x, y });
}

static int SongPanelEditorButtonAtPoint(SongPanelEditorData* data, int x, int y, std::vector<SbButton>* buttons = nullptr)
{
    std::vector<SbButton> local;
    if (!buttons) {
        SongPanelEditorBuildButtons(data, local);
        buttons = &local;
    }

    RECT card = SongPanelEditorCardRect(data);
    for (auto& b : *buttons) {
        if (b.id < 0) continue;
        RECT rc = b.rc;
        OffsetRect(&rc, 0, -data->scrollPos);
        if (b.id >= 3010 && b.id <= 3019) {
            int hitPad = (int)(4 * (data && data->ui ? data->ui->dpi : 1.0f) + 0.5f);
            if (hitPad < 4) hitPad = 4;
            InflateRect(&rc, hitPad, hitPad);
        }
        if (b.id >= 3010 && b.id <= 3019) {
            if (PtInRect(&rc, { x, y })) return b.id;
            continue;
        }
        if (!PtInRect(&card, { x, y }) && b.id != 3300 && b.id != 3301) continue;
        if (PtInRect(&rc, { x, y })) return b.id;
    }
    return -1;
}

static void SongPanelEditorDrawButton(HDC hdc, const SbButton& b, int scrollY, bool hovered)
{
    RECT rc = b.rc;
    OffsetRect(&rc, 0, -scrollY);
    COLORREF bg = b.active ? TmColor::ACCENT : (hovered ? TmColor::HOVER : TmColor::BG_CARD);
    TmDraw::FillRoundRect(hdc, rc, bg, TmRadius::SM);
    if (b.active) TmDraw::StrokeRoundRect(hdc, rc, TmColor::ACCENT_GLOW, TmRadius::SM, 1);
    wchar_t label[128];
    const wchar_t* text = b.label;
    if (b.id >= 3010 && b.id <= 3019) {
        swprintf_s(label, L"%d", b.id - 3009);
        text = label;
    }
    TmDraw::TextCenter(hdc, rc, text, b.active ? TmColor::TEXT_PRIMARY : TmColor::TEXT_SECOND,
        GetStockObject(DEFAULT_GUI_FONT) ? (HFONT)GetStockObject(DEFAULT_GUI_FONT) : nullptr);
}

static void SongPanelEditorPaint(SongPanelEditorData* data, HDC hdc)
{
    if (!data || !data->hwnd || !hdc) return;

    RECT rc = {};
    GetClientRect(data->hwnd, &rc);
    RECT card = SongPanelEditorCardRect(data);
    SongPanelEditorLayout lay = SongPanelEditorCalcLayout(data);
    auto S = [&](int v){ return (int)(v * (data->ui ? data->ui->dpi : 1.0f) + 0.5f); };
    RECT titleRc = { card.left + 18, card.top + 14, card.right - 18, card.top + 42 };
    RECT subRc = { card.left + 18, card.top + 38, card.right - 18, card.top + 58 };

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, max(1, rc.right), max(1, rc.bottom));
    HBITMAP oldBmp = nullptr;
    if (mem && bmp) {
        oldBmp = (HBITMAP)SelectObject(mem, bmp);
    }

    HDC drawHdc = mem ? mem : hdc;

    TmDraw::GradientV(drawHdc, rc, RGB(7, 9, 14), RGB(18, 20, 30));
    RECT topGlow = { 0, 0, rc.right, 72 };
    TmDraw::GradientV(drawHdc, topGlow, RGB(30, 40, 62), RGB(7, 9, 14));

    RECT shadow = { card.left + 8, card.top + 10, card.right + 8, card.bottom + 10 };
    TmDraw::FillRoundRect(drawHdc, shadow, RGB(4, 6, 10), 16);
    TmDraw::Card(drawHdc, card, RGB(20, 23, 34), 16, true);

    RECT accent = { card.left + 1, card.top + 1, card.right - 1, card.top + 4 };
    TmDraw::FillRoundRect(drawHdc, accent, RGB(88, 121, 214), 2);

    TmDraw::TextLeft(drawHdc, titleRc, L"Edit Now Playing Style", TmColor::TEXT_PRIMARY,
        data && data->ui ? data->ui->fonts.heading : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    TmDraw::TextLeft(drawHdc, subRc, L"Customize font, colors, rainbow & animations", TmColor::TEXT_THIRD,
        data && data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    std::vector<SbButton> buttons;
    SongPanelEditorBuildButtons(data, buttons);

    RECT clipCard = card;
    int oldMode = SetBkMode(drawHdc, TRANSPARENT);
    HRGN clip = CreateRectRgn(clipCard.left, clipCard.top, clipCard.right, clipCard.bottom);
    SelectClipRgn(drawHdc, clip);

    // Section labels
    RECT labels[] = {
        { lay.leftX, lay.headingY - S(20) - data->scrollPos, lay.leftX + lay.colW, lay.headingY - data->scrollPos },
        { lay.leftX, lay.songY - S(20) - data->scrollPos, lay.leftX + lay.colW, lay.songY - data->scrollPos },
        { lay.leftX, lay.animY - S(20) - data->scrollPos, lay.leftX + lay.colW, lay.animY - data->scrollPos },
        { lay.leftX, lay.alignY - S(20) - data->scrollPos, lay.leftX + lay.colW, lay.alignY - data->scrollPos },
    };
    TmDraw::TextLeft(drawHdc, labels[0], L"FONT & COLORS", TmColor::TEXT_THIRD, data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    TmDraw::TextLeft(drawHdc, labels[1], L"SONG STYLE", TmColor::TEXT_THIRD, data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    TmDraw::TextLeft(drawHdc, labels[2], L"ANIMATION", TmColor::TEXT_THIRD, data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    TmDraw::TextLeft(drawHdc, labels[3], L"ALIGNMENT", TmColor::TEXT_THIRD, data->ui ? data->ui->fonts.caption : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    for (auto& b : buttons) {
        if (b.id < 0) continue;
        bool hovered = data->hoverControl == b.id;
        SongPanelEditorDrawButton(drawHdc, b, data->scrollPos, hovered);
    }

    // Custom scrollbar
    RECT track = SongPanelEditorScrollTrackRect(data);
    RECT thumb = SongPanelEditorScrollThumbRect(data);
    int viewH = max(1, track.bottom - track.top);
    if (data->contentHeight > viewH) {
        TmDraw::FillRoundRect(drawHdc, track, TmColor::BG_DEEP, 6);
        TmDraw::FillRoundRect(drawHdc, thumb, TmColor::ACCENT, 5);
    }

    SelectClipRgn(drawHdc, nullptr);
    DeleteObject(clip);
    SetBkMode(drawHdc, oldMode);

    if (mem && bmp && oldBmp) {
        BitBlt(hdc, 0, 0, max(1, rc.right), max(1, rc.bottom), mem, 0, 0, SRCCOPY);
    }

    if (oldBmp) SelectObject(mem, oldBmp);
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
}

static LRESULT CALLBACK SongPanelEditorWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SongPanelEditorData* data = (SongPanelEditorData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    static HBRUSH sDlgBgBrush = nullptr;
    static COLORREF sDlgBg = RGB(14, 16, 24);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        data = (SongPanelEditorData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    }
    case WM_CREATE: {
        if (!sDlgBgBrush) sDlgBgBrush = CreateSolidBrush(sDlgBg);
        data->scrollPos = 0;
        data->contentHeight = 0;
        data->hoverControl = -1;
        data->pressedControl = -1;
        data->tracking = false;
        data->scrollDragging = false;
        data->scrollDragOffset = 0;

        std::vector<SbButton> buttons;
        SongPanelEditorBuildButtons(data, buttons);
        SongPanelEditorUpdateScrollBar(data);
        return 0;
    }
    case WM_SIZE:
        if (data) {
            SongPanelEditorSetScroll(data, data->scrollPos);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        float s = data && data->ui ? data->ui->dpi : TmDpi::Scale(hWnd);
        mmi->ptMinTrackSize.x = (int)(640 * s);
        mmi->ptMinTrackSize.y = (int)(540 * s);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!data) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int oldHover = data->hoverControl;
        data->hoverControl = -1;

        RECT track = SongPanelEditorScrollTrackRect(data);
        RECT thumb = SongPanelEditorScrollThumbRect(data);

        if (data->scrollDragging) {
            int thumbH = thumb.bottom - thumb.top;
            int trackRange = max(1, (track.bottom - track.top) - thumbH);
            int newY = y - data->scrollDragOffset;
            if (newY < track.top) newY = track.top;
            if (newY > track.top + trackRange) newY = track.top + trackRange;
            int maxScroll = max(0, data->contentHeight - max(1, (track.bottom - track.top)));
            int newPos = maxScroll > 0 ? (newY - track.top) * maxScroll / trackRange : 0;
            SongPanelEditorSetScroll(data, newPos);
        } else {
            std::vector<SbButton> buttons;
            SongPanelEditorBuildButtons(data, buttons);
            for (auto& b : buttons) {
                if (b.id < 0) continue;
                RECT rc = b.rc;
                OffsetRect(&rc, 0, -data->scrollPos);
                if (PtInRect(&rc, { x, y })) { data->hoverControl = b.id; break; }
            }
            if (PtInRect(&thumb, { x, y }) || PtInRect(&track, { x, y })) {
                data->hoverControl = data->hoverControl < 0 ? -2 : data->hoverControl;
            }
        }

        if (!data->tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            data->tracking = true;
        }
        if (oldHover != data->hoverControl) InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (data) {
            data->tracking = false;
            data->hoverControl = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!data) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT track = SongPanelEditorScrollTrackRect(data);
        RECT thumb = SongPanelEditorScrollThumbRect(data);
        if (PtInRect(&thumb, { x, y })) {
            data->scrollDragging = true;
            data->scrollDragOffset = y - thumb.top;
            SetCapture(hWnd);
            return 0;
        }
        if (PtInRect(&track, { x, y })) {
            int maxScroll = max(0, data->contentHeight - max(1, track.bottom - track.top));
            int thumbH = thumb.bottom - thumb.top;
            int trackRange = max(1, (track.bottom - track.top) - thumbH);
            int clickY = y - data->scrollDragOffset;
            if (clickY < track.top) clickY = track.top;
            if (clickY > track.top + trackRange) clickY = track.top + trackRange;
            int newPos = maxScroll > 0 ? (clickY - track.top) * maxScroll / trackRange : 0;
            SongPanelEditorSetScroll(data, newPos);
            return 0;
        }

        int id = SongPanelEditorButtonAtPoint(data, x, y);
        if (id >= 0) {
            data->pressedControl = id;
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (data && data->scrollDragging) {
            data->scrollDragging = false;
            if (GetCapture() == hWnd) ReleaseCapture();
            return 0;
        }
        if (data && data->pressedControl >= 0) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int id = SongPanelEditorButtonAtPoint(data, x, y);
            int pressed = data->pressedControl;
            data->pressedControl = -1;
            if (GetCapture() == hWnd) ReleaseCapture();
            InvalidateRect(hWnd, nullptr, FALSE);
            if (id == pressed) {
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
            }
            return 0;
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (data) {
            data->pressedControl = -1;
            data->scrollDragging = false;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (data) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            int step = max(32, delta / WHEEL_DELTA * 72);
            SongPanelEditorSetScroll(data, data->scrollPos - step);
        }
        return 0;
    case WM_KEYDOWN:
        if (data) {
            if (wp == VK_ESCAPE) {
                SendMessageW(hWnd, WM_COMMAND, IDCANCEL, 0);
                return 0;
            }
            if (wp == VK_RETURN) {
                SendMessageW(hWnd, WM_COMMAND, IDOK, 0);
                return 0;
            }
            if (wp == VK_UP) {
                SongPanelEditorSetScroll(data, data->scrollPos - 32);
                return 0;
            }
            if (wp == VK_DOWN) {
                SongPanelEditorSetScroll(data, data->scrollPos + 32);
                return 0;
            }
        }
        return 0;
    case WM_VSCROLL:
        if (data) {
            RECT track = SongPanelEditorScrollTrackRect(data);
            RECT thumb = SongPanelEditorScrollThumbRect(data);
            int maxScroll = max(0, data->contentHeight - max(1, track.bottom - track.top));
            int newPos = data->scrollPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: newPos -= 32; break;
                case SB_LINEDOWN: newPos += 32; break;
                case SB_PAGEUP: newPos -= max(32, (int)(thumb.bottom - thumb.top)); break;
                case SB_PAGEDOWN: newPos += max(32, (int)(thumb.bottom - thumb.top)); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    newPos = HIWORD(wp); // fallback for old-style notifications
                    break;
                case SB_TOP: newPos = 0; break;
                case SB_BOTTOM: newPos = maxScroll; break;
                default: break;
            }
            SongPanelEditorSetScroll(data, newPos);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORDLG:
        SetBkColor((HDC)wp, sDlgBg);
        return (LRESULT)sDlgBgBrush;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(220, 228, 244));
        return (LRESULT)sDlgBgBrush;
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, sDlgBg);
        SetTextColor((HDC)wp, RGB(235, 240, 250));
        return (LRESULT)sDlgBgBrush;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (data) {
            SongPanelEditorPaint(data, hdc);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (!data) break;
        switch (LOWORD(wp)) {
        case IDOK:
        case 3300:
            data->result = true;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        case IDCANCEL:
        case 3301:
            data->working = data->original;
            if (data->ui && data->panelIdx >= 0 && data->panelIdx < data->ui->numLayoutPanels) {
                data->ui->layoutPanels[data->panelIdx] = data->original;
                if (data->ui->host) data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
                InvalidateRect(data->ui->hWnd, nullptr, FALSE);
            }
            data->result = false;
            data->done = true;
            DestroyWindow(hWnd);
            return 0;
        case 3001: // Font...
            if (ChoosePanelFont(hWnd, data->working)) {
                wcscpy_s(data->working.headingFontName, data->working.fontName);
                ApplySongPanelEditorPreview(data);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        case 3002: // Heading Color
            if (ChoosePanelColorRGBA(hWnd, data->working.headingTextColor, data->working.headingTextAlpha)) {
                data->working.headingTextAlpha2 = data->working.headingTextAlpha;
            }
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3401: // Heading Gradient
            ChoosePanelColorRGBA(hWnd, data->working.headingTextColor2, data->working.headingTextAlpha2);
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3020: // Song Font
            if (ChooseFontFaceOnly(hWnd, data->working.songFontName, _countof(data->working.songFontName))) {
                ApplySongPanelEditorPreview(data);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        case 3021: // Song Color
            if (ChoosePanelColorRGBA(hWnd, data->working.songTextColor, data->working.songTextAlpha)) {
                data->working.songTextAlpha2 = data->working.songTextAlpha;
            }
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3402: // Song Gradient
            ChoosePanelColorRGBA(hWnd, data->working.songTextColor2, data->working.songTextAlpha2);
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3003: // Bg Color
            {
                BYTE bgAlpha = (BYTE)max(0, min(data->working.bgOpacity, 255));
                if (ChoosePanelColorRGBA(hWnd, data->working.bgColor, bgAlpha)) {
                    data->working.bgOpacity = bgAlpha;
                    data->working.hasBgColor = true;
                }
            }
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3403: // Bg Gradient
            {
                BYTE bgAlpha = (BYTE)max(0, min(data->working.bgOpacity, 255));
                if (ChoosePanelColorRGBA(hWnd, data->working.bgColor2, bgAlpha)) {
                    data->working.bgOpacity = bgAlpha;
                    data->working.hasBgColor = true;
                    data->working.bgStyle = 1;
                }
            }
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3004: // size -
            if (data->working.fontSize > 8) data->working.fontSize -= 2;
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3005: // size +
            if (data->working.fontSize < 200) data->working.fontSize += 2;
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3006: // Rainbow
            data->working.rainbowColor = !data->working.rainbowColor;
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3100: case 3101: case 3102: case 3103:
            data->working.songAnimMode = LOWORD(wp) - 3100;
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3010: case 3011: case 3012: case 3013: case 3014:
        case 3015: case 3016: case 3017: case 3018: case 3019:
            SetSongPanelSpeedLevel(data, LOWORD(wp) - 3010 + 1);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        case 3200: case 3201: case 3202:
            data->working.textAlign = LOWORD(wp) - 3200;
            ApplySongPanelEditorPreview(data);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (data) {
            data->working = data->original;
            if (data->ui && data->panelIdx >= 0 && data->panelIdx < data->ui->numLayoutPanels) {
                data->ui->layoutPanels[data->panelIdx] = data->original;
                if (data->ui->host) data->ui->host->UISetOverlayPanels(data->ui->layoutPanels, data->ui->numLayoutPanels);
                InvalidateRect(data->ui->hWnd, nullptr, FALSE);
            }
            data->done = true;
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (data) data->done = true;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static bool ShowSongPanelEditor(TmUIData* ui, int panelIdx)
{
    if (!ui || panelIdx < 0 || panelIdx >= ui->numLayoutPanels) return false;

    static bool registered = false;
    static const wchar_t* kClassName = L"TellyMediaSongPanelEditor";
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SongPanelEditorWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        registered = true;
    }

    SongPanelEditorData data = {};
    data.ui = ui;
    data.panelIdx = panelIdx;
    data.original = ui->layoutPanels[panelIdx];
    data.working = ui->layoutPanels[panelIdx];

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    RECT rc = { 0, 0, 860, 720 };
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    RECT ownerRc = {};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (ui->hWnd && GetWindowRect(ui->hWnd, &ownerRc)) {
        x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
        y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - height) / 2;
    }

    HWND box = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        L"Edit Now Playing Style",
        style,
        x, y, width, height,
        ui->hWnd,
        nullptr,
        GetModuleHandleW(nullptr),
        &data);

    if (!box) return false;

    data.hwnd = box;
    if (ui->hWnd) EnableWindow(ui->hWnd, FALSE);
    ShowWindow(box, SW_SHOW);
    UpdateWindow(box);

    MSG msg = {};
    while (!data.done) {
        if (GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(box, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    if (ui->hWnd) {
        EnableWindow(ui->hWnd, TRUE);
        SetForegroundWindow(ui->hWnd);
    }

    return data.result;
}

// ─── Panel image helpers ──────────────────────────────────────────────────────
// Open a file dialog and assign the selected image to the given panel index.
static void LoadImageForPanel(TmUIData* d, int panelIdx)
{
    if (panelIdx < 0 || panelIdx >= d->numLayoutPanels) return;
    OPENFILENAMEW ofn = { sizeof(ofn) };
    wchar_t file[MAX_PATH] = L"";
    ofn.hwndOwner = d->hWnd;
    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select Image";
    if (GetOpenFileNameW(&ofn)) {
        OverlayPanel& p = d->layoutPanels[panelIdx];
        wcscpy_s(p.imagePath, file);
        p.hasImage = true;
        ClearPanelImageCache(d, panelIdx); // force reload of preview cache
        TM_INFO("Loaded image for panel %d: %S", panelIdx, file);
        if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
        TmUI::SaveState(d);
        RedrawWindow(d->hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

// Remove the image from a panel (clear path + flag).
static void RemoveImageFromPanel(TmUIData* d, int panelIdx)
{
    if (panelIdx < 0 || panelIdx >= d->numLayoutPanels) return;
    OverlayPanel& p = d->layoutPanels[panelIdx];
    p.imagePath[0] = L'\0';
    p.hasImage = false;
    ClearPanelImageCache(d, panelIdx);
    TM_INFO("Removed image from panel %d", panelIdx);
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TmUI::SaveState(d);
    RedrawWindow(d->hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static void HandleLayoutSidebarClick(TmUIData* d, int id)
{
    TM_INFO("HandleLayoutSidebarClick: id=%d", id);
    // Panel list selection
    if (id >= LB_PANEL_SELECT_BASE && id < LB_PANEL_SELECT_BASE + (int)MAX_OVERLAY_PANELS) {
        int idx = id - LB_PANEL_SELECT_BASE;
        if (idx >= 0 && idx < d->numLayoutPanels) {
            d->selectedPanel = idx;
            InvalidateRect(d->hWnd, nullptr, FALSE);
        }
        return;
    }
    switch (id) {
    case LB_ADD_MAIN:   AddLayoutPanel(d, 0); break;
    case LB_ADD_IMAGE:  AddLayoutPanel(d, 1); break;
    case LB_ADD_TEXT:   AddLayoutPanel(d, 2); break;
    case LB_ADD_SHADER: AddLayoutPanel(d, 3); break;
    case LB_ADD_SONG:   AddLayoutPanel(d, 4); break;
    case LB_POS_TL: ApplyPanelPreset(d, 0); break;
    case LB_POS_TC: ApplyPanelPreset(d, 1); break;
    case LB_POS_TR: ApplyPanelPreset(d, 2); break;
    case LB_POS_ML: ApplyPanelPreset(d, 3); break;
    case LB_POS_MC: ApplyPanelPreset(d, 4); break;
    case LB_POS_MR: ApplyPanelPreset(d, 5); break;
    case LB_POS_BL: ApplyPanelPreset(d, 6); break;
    case LB_POS_BC: ApplyPanelPreset(d, 7); break;
    case LB_POS_BR: ApplyPanelPreset(d, 8); break;
    case LB_POS_FULL: ApplyPanelPreset(d, 9); break;
    case LB_Z_UP: ChangePanelZ(d, 1); break;
    case LB_Z_DOWN: ChangePanelZ(d, -1); break;
    case LB_DELETE: DeleteLayoutPanel(d, d->selectedPanel); break;
    case LB_DIRECT_SELECT: d->directSelectEnabled = !d->directSelectEnabled; break;
    case IDC_TM_LAYTAB_PRESET_SAVE: {
        wchar_t path[MAX_PATH] = {};
        if (PromptLayoutProfileFile(d->hWnd, path, true)) {
            SaveLayoutProfileToFile(d, path);
        }
        break;
    }
    case IDC_TM_LAYTAB_PRESET_LOAD: {
        ShowLayoutProfileMenu(d);
        break;
    }
    case LB_SHADER_PREV: {
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels && d->layoutPanels[d->selectedPanel].panelType == 3) {
            wchar_t shaderNames[kShaderNameBufferChars] = L"No shaders";
            int count = GetShaderCountAndNames(d, shaderNames, kShaderNameBufferChars);
            if (count > 0) {
                int cur = d->layoutPanels[d->selectedPanel].shaderIndex;
                if (cur < 0 || cur >= count) cur = 0;
                ApplyShaderSelectionToPanel(d, (cur - 1 + count) % count);
            }
        }
        break;
    }
    case LB_SHADER_PICKER:
        ShowShaderPickerMenu(d);
        break;
    case LB_SHADER_NEXT: {
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels && d->layoutPanels[d->selectedPanel].panelType == 3) {
            wchar_t shaderNames[kShaderNameBufferChars] = L"No shaders";
            int count = GetShaderCountAndNames(d, shaderNames, kShaderNameBufferChars);
            if (count > 0) {
                int cur = d->layoutPanels[d->selectedPanel].shaderIndex;
                if (cur < 0 || cur >= count) cur = 0;
                ApplyShaderSelectionToPanel(d, (cur + 1) % count);
            }
        }
        break;
    }
    case LB_SHADER_RELOAD:
        if (d->host) d->host->HostReloadShaders();
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels && d->layoutPanels[d->selectedPanel].panelType == 3) {
            ApplyShaderSelectionToPanel(d, d->layoutPanels[d->selectedPanel].shaderIndex);
        }
        break;
    case LB_LOAD_IMAGE:
        LoadImageForPanel(d, d->selectedPanel);
        break;
    case LB_IMAGE_ANIM_0: case LB_IMAGE_ANIM_1: case LB_IMAGE_ANIM_2: case LB_IMAGE_ANIM_3: case LB_IMAGE_ANIM_4: {
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
            OverlayPanel& p = d->layoutPanels[d->selectedPanel];
            if (p.panelType == 1) {
                p.imageAnimMode = (int)(id - LB_IMAGE_ANIM_0);
                if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
                TmUI::SaveState(d);
                RedrawWindow(d->hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            }
        }
        break; }
    case LB_EDIT_TEXT: {
        ShowTextPanelEditor(d, d->selectedPanel);
        break;
    }
    case LB_TEXT_FONT: {
        ShowTextPanelEditor(d, d->selectedPanel);
        break;
    }
    case LB_TEXT_SIZE_DEC: {
        ShowTextPanelEditor(d, d->selectedPanel);
        break;
    }
    case LB_TEXT_SIZE_INC: {
        ShowTextPanelEditor(d, d->selectedPanel);
        break;
    }
    case LB_TEXT_COLOR: {
        ShowTextPanelEditor(d, d->selectedPanel);
        break;
    }
    case LB_BG_COLOR: {
        ShowTextPanelEditor(d, d->selectedPanel);
        break;
    }
    }
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

static void PaintLayoutCanvas(TmUIData* d, HDC hdc)
{
    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };

    // Draw canvas background (16:9 output preview)
    RECT rc = d->rcCanvas;
    TmDraw::FillRoundRect(hdc, rc, RGB(0, 0, 0), TmRadius::SM);
    RECT inner = { rc.left + S(2), rc.top + S(2), rc.right - S(2), rc.bottom - S(2) };
    TmDraw::FillRoundRect(hdc, inner, RGB(12, 12, 16), TmRadius::SM);

    // Draw panels (sorted by zOrder for correct layering)
    int sorted[MAX_OVERLAY_PANELS];
    for (int i = 0; i < d->numLayoutPanels; ++i) sorted[i] = i;
    for (int i = 0; i < d->numLayoutPanels - 1; ++i)
        for (int j = i + 1; j < d->numLayoutPanels; ++j)
            if (d->layoutPanels[sorted[i]].zOrder > d->layoutPanels[sorted[j]].zOrder)
                { int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }

    for (int i = 0; i < d->numLayoutPanels; ++i) {
        OverlayPanel& p = d->layoutPanels[sorted[i]];
        if (!p.visible) continue;
        RECT pr = PanelRectPx(d, p);

        // Main panel: show current media output
        if (p.panelType == 0) {
            HBRUSH bg = CreateSolidBrush(RGB(10, 40, 18));
            FillRect(hdc, &pr, bg);
            DeleteObject(bg);
            RECT tr = pr;
            TmDraw::TextCenter(hdc, tr, L"Main Output", TmColor::TEXT_PRIMARY, d->fonts.body);
        } else if (p.panelType == 1 && p.hasImage && p.imagePath[0]) {
            // Image panel: draw image over whatever is already on the HDC (e.g. the green
            // main panel) so transparent areas in the PNG show through correctly.
            int ci = sorted[i];
            if (!d->panelImageCache[ci]) {
                d->panelImageCache[ci] = Image::FromFile(p.imagePath);
                if (d->panelImageCache[ci] && d->panelImageCache[ci]->GetLastStatus() != Ok) {
                    delete d->panelImageCache[ci];
                    d->panelImageCache[ci] = nullptr;
                }
            }
            if (d->panelImageCache[ci]) {
                Graphics graphics(hdc);
                graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.DrawImage(d->panelImageCache[ci], pr.left, pr.top,
                                   pr.right - pr.left, pr.bottom - pr.top);
            } else {
                RECT tr = pr;
                TmDraw::TextCenter(hdc, tr, L"Image (Load Failed)", TmColor::TEXT_SECOND, d->fonts.caption);
            }
        } else if (p.panelType == 4) {
            wchar_t nowPlaying[256] = {};
            GetNowPlayingText(d, nowPlaying, 256);

            COLORREF bg = p.bgColor;
            if (p.bgOpacity < 255) {
                int r = GetRValue(bg) * p.bgOpacity / 255;
                int g = GetGValue(bg) * p.bgOpacity / 255;
                int b = GetBValue(bg) * p.bgOpacity / 255;
                bg = RGB(r, g, b);
            }
            TmDraw::FillRoundRect(hdc, pr, bg, TmRadius::SM);

            RECT titleRc = pr;
            titleRc.left += S(8);
            titleRc.top += S(8);
            titleRc.right -= S(8);
            wchar_t title[64];
            swprintf_s(title, L"%s", p.textContent[0] ? p.textContent : L"Now Playing");
            TmDraw::TextLeft(hdc, titleRc, title, p.textColor, d->fonts.heading);

            RECT songRc = pr;
            songRc.left += S(8);
            songRc.top += S(36);
            songRc.right -= S(8);
            if (!nowPlaying[0]) wcscpy_s(nowPlaying, L"No Track Playing");
            TmDraw::TextLeft(hdc, songRc, nowPlaying, TmColor::TEXT_PRIMARY, d->fonts.body);
        } else {
            // Panel background (with opacity simulation via alpha blend)
            COLORREF bg = p.bgColor;
            if (p.bgOpacity < 255) {
                int r = GetRValue(bg) * p.bgOpacity / 255;
                int g = GetGValue(bg) * p.bgOpacity / 255;
                int b = GetBValue(bg) * p.bgOpacity / 255;
                bg = RGB(r, g, b);
            }
            TmDraw::FillRoundRect(hdc, pr, bg, TmRadius::SM);
        }

        // Text content preview
        if (p.panelType == 2 && p.textContent[0]) {
            RECT tr = pr;
            tr.left += S(4); tr.right -= S(4);
            TmDraw::TextLeft(hdc, tr, p.textContent, p.textColor, d->fonts.body);
        } else if (p.panelType != 0) {
            // Type label (not for main panel)
            RECT tr = pr;
            tr.left += S(4); tr.top += S(4);
            wchar_t label[64];
            swprintf_s(label, L"%s", PanelTypeName(p.panelType));
            TmDraw::TextLeft(hdc, tr, label, TmColor::TEXT_SECOND, d->fonts.caption);
        }

        // Draw subtle outline for all panels
        HPEN panelPen = CreatePen(PS_SOLID, S(1), RGB(80, 80, 90));
        HPEN oldPanelPen = (HPEN)SelectObject(hdc, panelPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, pr.left, pr.top, pr.right, pr.bottom, S(4), S(4));
        SelectObject(hdc, oldPanelPen);
        DeleteObject(panelPen);
    }

    // Draw selection outline on top of ALL panels so it is never buried under images.
    if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
        OverlayPanel& sp = d->layoutPanels[d->selectedPanel];
        if (sp.visible) {
            RECT pr = PanelRectPx(d, sp);
            HPEN pen = CreatePen(PS_SOLID, S(2), TmColor::ACCENT);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, pr.left, pr.top, pr.right, pr.bottom, S(4), S(4));
            SelectObject(hdc, old);
            DeleteObject(pen);
            // 4 corner resize handles - draw at dot size, hit area is larger
            for (int c = 0; c < 4; ++c) {
                RECT dr = PanelCornerDraw(d, sp, c);
                TmDraw::FillRoundRect(hdc, dr, TmColor::ACCENT, 2);
            }
        }
    }
}

// ─── Options / License pages ─────────────────────────────────────────────────
// Buttons are laid out in centered cards with grouped rows. The same geometry
// is used by paint + hit-test so clicks and drawing always agree.
static void BuildOptionsControls(TmUIData* d, std::vector<SbButton>& out)
{
    out.clear();
    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };

    // Card spans the content area (rcGrid), capped to a comfortable width.
    RECT area = d->rcGrid;
    int pad = S(TmSpace::MD);
    int cardL = area.left + pad;
    int cardR = area.right - pad;
    int maxW = S(560);
    if (cardR - cardL > maxW) cardR = cardL + maxW;
    int x = cardL;
    int w = cardR - cardL;
    int y = area.top + pad + S(28);          // leave room for first header
    int bh = S(34), gap = S(8), hdr = S(40);  // hdr = header+spacing per group
    int third = (w - gap * 2) / 3;

    const int scaleIds[3] = { OPT_SCALE_ASPECT, OPT_SCALE_STRETCH, OPT_SCALE_NOSCALE };
    const wchar_t* scaleLbl[3] = { L"Aspect", L"Stretch", L"No Scale" };
    for (int c = 0; c < 3; ++c) {
        int bx = x + c * (third + gap);
        out.push_back({ { bx, y, bx + third, y + bh }, scaleIds[c], scaleLbl[c], d->sidebar.scaleMode == c, false });
    }
    y += bh + hdr;

    const int transIds[3] = { OPT_TRANS_CUT, OPT_TRANS_FADE, OPT_TRANS_CROSS };
    const wchar_t* transLbl[3] = { L"Cut", L"Fade", L"Crossfade" };
    for (int c = 0; c < 3; ++c) {
        int bx = x + c * (third + gap);
        out.push_back({ { bx, y, bx + third, y + bh }, transIds[c], transLbl[c], d->sidebar.ssTransition == c, false });
    }
    y += bh + hdr;

    const int dirIds[3] = { OPT_DIR_FWD, OPT_DIR_BACK, OPT_DIR_RANDOM };
    const wchar_t* dirLbl[3] = { L"Forward", L"Backward", L"Random" };
    for (int c = 0; c < 3; ++c) {
        int bx = x + c * (third + gap);
        out.push_back({ { bx, y, bx + third, y + bh }, dirIds[c], dirLbl[c], d->sidebar.ssDirection == c, false });
    }
    y += bh + hdr;

    // Slideshow row: Loop toggle | Delay - | Delay value (drawn) | Delay +
    int half = (w - gap) / 2;
    out.push_back({ { x, y, x + half, y + bh }, OPT_LOOP_TOGGLE,
                    d->sidebar.ssLoop ? L"Loop: On" : L"Loop: Off", d->sidebar.ssLoop, true });
    int stepW = S(40);
    int decX = x + half + gap;
    int incX = cardR - stepW;
    out.push_back({ { decX, y, decX + stepW, y + bh }, OPT_DELAY_DEC, L"-", false, false });
    out.push_back({ { incX, y, incX + stepW, y + bh }, OPT_DELAY_INC, L"+", false, false });
    y += bh + hdr;

    // Shader section
    out.push_back({ { x, y, x + w, y + bh }, OPT_SHADER_ENABLE,
                    d->sidebar.shadersEnabled ? L"Enable Shaders: On" : L"Enable Shaders: Off",
                    d->sidebar.shadersEnabled, true });
    y += bh + gap;

    out.push_back({ { x, y, x + w, y + bh }, OPT_SHADER_KARAOKE_DISABLE,
                    d->sidebar.shaderDisableOnKaraoke ? L"Disable on Karaoke: On" : L"Disable on Karaoke: Off",
                    d->sidebar.shaderDisableOnKaraoke, true });
    y += bh + hdr;

    // Shader selector: Prev | Name (drawn) | Next | Reload
    int btnW = S(60);
    int nameW = w - btnW * 3 - gap * 2;
    out.push_back({ { x, y, x + btnW, y + bh }, OPT_SHADER_PREV, L"<", false, false });
    out.push_back({ { x + btnW + gap, y, x + btnW + gap + nameW, y + bh }, 0, L"", false, false }); // name placeholder
    out.push_back({ { x + btnW + gap + nameW + gap, y, x + btnW * 2 + gap + nameW + gap, y + bh }, OPT_SHADER_NEXT, L">", false, false });
    out.push_back({ { cardR - btnW, y, cardR, y + bh }, OPT_SHADER_RELOAD, L"Reload", false, false });
    y += bh + hdr;

    out.push_back({ { x, y, x + w, y + bh }, OPT_KARAOKE_AUTOHIDE,
                    d->karaokeAutoHide ? L"Disable Panels/Slideshow on Karaoke: On" : L"Disable Panels/Slideshow on Karaoke: Off",
                    d->karaokeAutoHide, true });
    y += bh + hdr;

    out.push_back({ { x, y, x + w, y + bh }, OPT_BANKS_EDIT, L"Bank Names & Colors...", false, false });
}

static void BuildLicenseControls(TmUIData* d, std::vector<SbButton>& out)
{
    out.clear();
    out.push_back({ d->rcLicenseButtons[0], OPT_LICENSE_LOGIN, L"Login", false, false });
    out.push_back({ d->rcLicenseButtons[1], OPT_LICENSE_VALIDATE, L"Validate Now", false, false });
    out.push_back({ d->rcLicenseButtons[2], OPT_LICENSE_ACTIVATE, L"Activate Device", false, false });
    
    LicenseState st = {};
    TmLicense::Init(&st);
    if (d->host && d->host->HostGetLicenseState(&st)) {
        if (st.status == LIC_VALID) {
            out.push_back({ d->rcLicenseButtons[3], OPT_LICENSE_LOGOUT, L"Logout", false, false });
        }
    }
}

static void PaintOptionsPage(TmUIData* d, HDC hdc)
{
    auto S = [&](int v){ return (int)(v * d->dpi + 0.5f); };

    std::vector<SbButton> btns;
    BuildOptionsControls(d, btns);

    auto header = [&](int firstId, const wchar_t* text) {
        for (auto& b : btns) if (b.id == firstId) {
            RECT h = { b.rc.left, b.rc.top - S(24), b.rc.right + S(200), b.rc.top - S(4) };
            TmDraw::TextLeft(hdc, h, text, TmColor::TEXT_THIRD, d->fonts.caption);
            break;
        }
    };
    header(OPT_SCALE_ASPECT, L"SCALE MODE");
    header(OPT_TRANS_CUT,    L"TRANSITION");
    header(OPT_DIR_FWD,      L"SLIDESHOW DIRECTION");
    header(OPT_LOOP_TOGGLE,  L"SLIDESHOW");
    header(OPT_SHADER_ENABLE, L"SHADERS");
    header(OPT_KARAOKE_AUTOHIDE, L"KARAOKE");
    header(OPT_BANKS_EDIT,    L"BANKS");

    for (auto& b : btns) {
        bool hover = d->hoverSidebar == b.id;
        COLORREF bg = b.active ? TmColor::ACCENT : (hover ? TmColor::HOVER : TmColor::BG_CARD);
        TmDraw::FillRoundRect(hdc, b.rc, bg, TmRadius::SM);
        TmDraw::TextCenter(hdc, b.rc, b.label,
            b.active ? TmColor::TEXT_PRIMARY : TmColor::TEXT_SECOND, d->fonts.body);

        // Draw the delay value between the - and + buttons
        if (b.id == OPT_DELAY_DEC) {
            RECT vr = { b.rc.right + S(4), b.rc.top, b.rc.right + S(120), b.rc.bottom };
            wchar_t val[48];
            swprintf_s(val, L"%ds", d->sidebar.ssDelaySec);
            TmDraw::TextCenter(hdc, vr, val, TmColor::TEXT_PRIMARY, d->fonts.body);
        }

        // Draw the current shader name between Prev and Next buttons
        if (b.id == OPT_SHADER_PREV) {
            RECT nr = { b.rc.right + S(4), b.rc.top, b.rc.right + S(4) + (b.rc.right - b.rc.left - S(120)), b.rc.bottom };
            wchar_t shaderNames[kShaderNameBufferChars] = L"No shaders";
            if (d->host) d->host->HostGetShaderNames(shaderNames, kShaderNameBufferChars);
            // Parse pipe-separated names and get the current one
            wchar_t* ctx = nullptr;
            wchar_t* token = wcstok_s(shaderNames, L"|", &ctx);
            int idx = 0;
            while (token && idx < d->sidebar.mainShaderIndex) {
                token = wcstok_s(nullptr, L"|", &ctx);
                idx++;
            }
            const wchar_t* name = token ? token : L"None";
            TmDraw::TextCenter(hdc, nr, name, TmColor::TEXT_PRIMARY, d->fonts.body);
        }
    }
}

static void PaintLicensePage(TmUIData* d, HDC hdc)
{
    std::vector<SbButton> btns;
    BuildLicenseControls(d, btns);

    if (d->rcLicenseLogo.right > d->rcLicenseLogo.left && d->rcLicenseLogo.bottom > d->rcLicenseLogo.top) {
        static Gdiplus::Image* sLicensePageLogo = nullptr;
        if (!sLicensePageLogo && d->hInst) {
            sLicensePageLogo = LoadPngFromResource(d->hInst, 102);
        }
        if (sLicensePageLogo && sLicensePageLogo->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            int logoW = sLicensePageLogo->GetWidth();
            int logoH = sLicensePageLogo->GetHeight();
            int availW = d->rcLicenseLogo.right - d->rcLicenseLogo.left;
            int availH = d->rcLicenseLogo.bottom - d->rcLicenseLogo.top;
            float scale = min((float)availW / logoW, (float)availH / logoH);
            int drawW = (int)(logoW * scale);
            int drawH = (int)(logoH * scale);
            int drawX = d->rcLicenseLogo.left + (availW - drawW) / 2;
            int drawY = d->rcLicenseLogo.top + (availH - drawH) / 2;
            graphics.DrawImage(sLicensePageLogo, drawX, drawY, drawW, drawH);
        }
    }

    auto Scale = [&](int v){ return (int)(v * d->dpi + 0.5f); };
    int labelH = Scale(18);
    int labelGap = Scale(4);
    int labelLeft = d->rcLicenseEmail.left;
    if (d->rcLicenseEmail.right > d->rcLicenseEmail.left && d->rcLicenseEmail.bottom > d->rcLicenseEmail.top) {
        RECT emailLabel = { labelLeft, d->rcLicenseEmail.top - labelGap - labelH, d->rcLicenseEmail.right, d->rcLicenseEmail.top - labelGap };
        TmDraw::TextLeft(hdc, emailLabel, L"Email", TmColor::TEXT_PRIMARY, d->fonts.body);
    }
    if (d->rcLicensePassword.right > d->rcLicensePassword.left && d->rcLicensePassword.bottom > d->rcLicensePassword.top) {
        RECT passwordLabel = { labelLeft, d->rcLicensePassword.top - labelGap - labelH, d->rcLicensePassword.right, d->rcLicensePassword.top - labelGap };
        TmDraw::TextLeft(hdc, passwordLabel, L"Password", TmColor::TEXT_PRIMARY, d->fonts.body);
    }
    if (d->rcLicenseButtons[0].right > d->rcLicenseButtons[0].left && d->rcLicenseButtons[0].bottom > d->rcLicenseButtons[0].top) {
        RECT loginLabel = { labelLeft, d->rcLicenseButtons[0].top - labelGap - labelH, d->rcLicenseButtons[0].right, d->rcLicenseButtons[0].top - labelGap };
        TmDraw::TextLeft(hdc, loginLabel, L"Sign In", TmColor::TEXT_PRIMARY, d->fonts.body);
    }

    RECT card = d->rcLicenseCard;
    if (card.right > card.left && card.bottom > card.top) {
        TmDraw::Card(hdc, card, TmColor::BG_ELEVATED, TmRadius::MD, true);

        wchar_t badgeText[64] = {};
        COLORREF badgeBg = TmColor::BG_CARD;
        COLORREF badgeFg = TmColor::TEXT_SECOND;
        LicenseBadgeInfo(d, badgeText, _countof(badgeText), &badgeBg, &badgeFg);
        TmDraw::Card(hdc, d->rcLicenseBadge, badgeBg, TmRadius::MD, false);
        TmDraw::TextCenter(hdc, d->rcLicenseBadge, badgeText, badgeFg, d->fonts.caption);

        wchar_t details[256] = {};
        LicenseStatusDetails(d, details, _countof(details));
        TmDraw::TextLeft(hdc, d->rcLicenseDetails, details[0] ? details : L"Manage sign-in, validation, activation, and secure credential storage here.", TmColor::TEXT_SECOND, d->fonts.body);

        wchar_t statusText[256] = {};
        LicenseStatusText(d, statusText, _countof(statusText));
        TmDraw::TextLeft(hdc, d->rcLicenseStatus, statusText, TmColor::TEXT_THIRD, d->fonts.caption);
    }

    for (auto& b : btns) {
        bool hover = d->hoverSidebar == b.id;
        COLORREF bg = b.active ? TmColor::ACCENT : (hover ? TmColor::HOVER : TmColor::BG_CARD);
        TmDraw::FillRoundRect(hdc, b.rc, bg, TmRadius::SM);
        TmDraw::TextCenter(hdc, b.rc, b.label, TmColor::TEXT_PRIMARY, d->fonts.body);
    }
}

static void HandleOptionsClick(TmUIData* d, int id)
{
    TM_INFO("HandleOptionsClick: id=%d", id);
    switch (id) {
    case OPT_SCALE_ASPECT:  d->sidebar.scaleMode = SCALE_ASPECT;  if (d->host) d->host->HostScaleMode(SCALE_ASPECT);  break;
    case OPT_SCALE_STRETCH: d->sidebar.scaleMode = SCALE_STRETCH; if (d->host) d->host->HostScaleMode(SCALE_STRETCH); break;
    case OPT_SCALE_NOSCALE: d->sidebar.scaleMode = SCALE_NOSCALE; if (d->host) d->host->HostScaleMode(SCALE_NOSCALE); break;
    case OPT_TRANS_CUT:   d->sidebar.ssTransition = 0; if (d->host) d->host->HostTransitionMode(0); break;
    case OPT_TRANS_FADE:  d->sidebar.ssTransition = 1; if (d->host) d->host->HostTransitionMode(1); break;
    case OPT_TRANS_CROSS: d->sidebar.ssTransition = 2; if (d->host) d->host->HostTransitionMode(2); break;
    case OPT_DIR_FWD:    d->sidebar.ssDirection = 0; break;
    case OPT_DIR_BACK:   d->sidebar.ssDirection = 1; break;
    case OPT_DIR_RANDOM: d->sidebar.ssDirection = 2; break;
    case OPT_LOOP_TOGGLE: d->sidebar.ssLoop = !d->sidebar.ssLoop; break;
    case OPT_DELAY_DEC: if (d->sidebar.ssDelaySec > 1)  d->sidebar.ssDelaySec--; break;
    case OPT_DELAY_INC: if (d->sidebar.ssDelaySec < 60) d->sidebar.ssDelaySec++; break;
    case OPT_SHADER_ENABLE:
        d->sidebar.shadersEnabled = !d->sidebar.shadersEnabled;
        if (d->host) d->host->HostSetShadersEnabled(d->sidebar.shadersEnabled);
        break;
    case OPT_SHADER_KARAOKE_DISABLE:
        d->sidebar.shaderDisableOnKaraoke = !d->sidebar.shaderDisableOnKaraoke;
        if (d->host) d->host->HostSetShaderKaraokeDisable(d->sidebar.shaderDisableOnKaraoke);
        break;
    case OPT_SHADER_PREV:
        if (d->sidebar.mainShaderIndex > 0) d->sidebar.mainShaderIndex--;
        if (d->host) d->host->HostSetMainShader(d->sidebar.mainShaderIndex);
        break;
    case OPT_SHADER_NEXT:
        d->sidebar.mainShaderIndex++;
        if (d->host) d->host->HostSetMainShader(d->sidebar.mainShaderIndex);
        break;
    case OPT_SHADER_RELOAD:
        if (d->host) d->host->HostReloadShaders();
        break;
    case OPT_KARAOKE_AUTOHIDE:
        d->karaokeAutoHide = !d->karaokeAutoHide;
        if (d->host) d->host->HostSetKaraokeAutoHide(d->karaokeAutoHide);
        break;
    case OPT_BANKS_EDIT:
        ShowBankConfigDialog(d);
        break;
    }
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

static void HandleLicenseClick(TmUIData* d, int id)
{
    TM_INFO("HandleLicenseClick: id=%d", id);
    switch (id) {
    case OPT_LICENSE_LOGIN:
        if (!d->editLicenseEmail || !d->editLicensePassword || !d->chkLicenseSave) break;
        {
            wchar_t email[256] = {};
            wchar_t password[256] = {};
            GetWindowTextW(d->editLicenseEmail, email, _countof(email));
            GetWindowTextW(d->editLicensePassword, password, _countof(password));
            bool save = (SendMessageW(d->chkLicenseSave, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool success = (d->host && d->host->HostLicenseLogin(email, password, save));
            if (!success) {
                LicenseState st = {};
                TmLicense::Init(&st);
                if (d->host) d->host->HostGetLicenseState(&st);
                wchar_t msg[256] = L"License login failed.";
                if (st.message[0]) MultiByteToWideChar(CP_UTF8, 0, st.message, -1, msg, _countof(msg));
                MessageBoxW(d->hWnd, msg, L"TellyMedia License", MB_OK | MB_ICONWARNING);
                break;
            }
            if (d->editLicensePassword) SetWindowTextW(d->editLicensePassword, L"");
        }
        break;
    case OPT_LICENSE_VALIDATE:
        if (d->host) d->host->HostLicenseValidate();
        break;
    case OPT_LICENSE_ACTIVATE:
        if (d->host) d->host->HostLicenseActivate();
        break;
    case OPT_LICENSE_LOGOUT:
        if (d->host) d->host->HostLicenseLogout();
        break;
    }
    InvalidateRect(d->hWnd, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TmUIData* d = (TmUIData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        d = (TmUIData*)cs->lpCreateParams;
        d->hWnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->dpi = TmDpi::Scale(hwnd);
        d->fonts.Create(d->dpi);
        EnsureCommonControlsInit();

        d->editLicenseEmail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_LICENSE_EMAIL, nullptr, nullptr);
        d->editLicensePassword = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
            0, 0, 0, 0, hwnd, (HMENU)IDC_LICENSE_PASSWORD, nullptr, nullptr);
        d->chkLicenseSave = CreateWindowExW(0, L"BUTTON", L"Save password in Windows Credential Manager",
            WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, hwnd, (HMENU)IDC_LICENSE_SAVE, nullptr, nullptr);

        LicenseState st = {};
        TmLicense::Init(&st);
        if (d->host && d->host->HostGetLicenseState(&st)) {
            if (st.userEmail[0] && d->editLicenseEmail) {
                SetWindowTextW(d->editLicenseEmail, st.userEmail);
            }
            if (d->chkLicenseSave) {
                SendMessageW(d->chkLicenseSave, BM_SETCHECK, st.savePassword ? BST_CHECKED : BST_UNCHECKED, 0);
            }
        }
        DragAcceptFiles(hwnd, TRUE);

        ComputeLayout(d);
        SetTimer(hwnd, 2, 500, nullptr);
        if (d->sidebar.ssEnabled && d->sidebar.ssRunning) {
            StartSlideshow(d);
        }
        return 0;
    }
    case WM_SIZE:
        ComputeLayout(d);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        d->dpi = TmDpi::Scale(hwnd);
        d->fonts.Create(d->dpi);
        ComputeLayout(d);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        float s = TmDpi::Scale(hwnd);
        mmi->ptMinTrackSize.x = (int)(760 * s);
        mmi->ptMinTrackSize.y = (int)(520 * s);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (!d) return HTCLIENT;

        RECT rc = {};
        GetClientRect(hwnd, &rc);
        int border = max(6, (int)(6 * TmDpi::Scale(hwnd) + 0.5f));
        bool onLeft = pt.x >= 0 && pt.x < border;
        bool onRight = pt.x < rc.right && pt.x >= rc.right - border;
        bool onTop = pt.y >= 0 && pt.y < border;
        bool onBottom = pt.y < rc.bottom && pt.y >= rc.bottom - border;

        if (onLeft && onTop) return HTTOPLEFT;
        if (onRight && onTop) return HTTOPRIGHT;
        if (onLeft && onBottom) return HTBOTTOMLEFT;
        if (onRight && onBottom) return HTBOTTOMRIGHT;
        if (onLeft) return HTLEFT;
        if (onRight) return HTRIGHT;
        if (onTop) return HTTOP;
        if (onBottom) return HTBOTTOM;

        // Keep tab buttons and close button clickable; only empty header space should drag.
        for (int i = 0; i < TAB_COUNT; ++i) {
            if (PtInRect(&d->rcTabs[i], pt)) {
                return HTCLIENT;
            }
        }
        if (PtInRect(&d->rcCloseBtn, pt)) {
            return HTCLIENT;
        }

        int topBarHeight = 0;
        if (d) {
            topBarHeight = d->metrics.headerH;
        }
        // Fallback to default header height if data not available
        if (topBarHeight <= 0) topBarHeight = (int)(50 * TmDpi::Scale(hwnd));
        if (pt.y >= 0 && pt.y < topBarHeight) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        OnPaint(d);
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int oldSlot = d->hoverSlot, oldBank = d->hoverBank, oldTab = d->hoverTab, oldSb = d->hoverSidebar;
        int oldPanel = d->hoverPanel;
        bool oldHoverClose = d->hoverCloseBtn;
        
        // Handle scrollbar drag
        if (d->scrollDragging) {
            if (d->scrollDragArea == 0) { // grid
                RECT track = GridScrollTrackRect(d);
                int viewH = d->rcGrid.bottom - d->rcGrid.top;
                int maxScroll = max(0, d->contentHeight - viewH);
                int trackRange = track.bottom - track.top;
                int thumbH = max((int)(30*d->dpi), viewH * viewH / max(1, d->contentHeight));
                int newY = y - d->scrollDragOffset;
                int newPos = maxScroll > 0 ? (newY - track.top) * maxScroll / trackRange : 0;
                if (newPos < 0) newPos = 0;
                if (newPos > maxScroll) newPos = maxScroll;
                d->scrollY = newPos;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if (d->scrollDragArea == 1) { // sidebar
                RECT track = SidebarScrollTrackRect(d);
                int viewH = d->rcSidebar.bottom - d->rcSidebar.top;
                int maxScroll = max(0, d->sidebarContentHeight - viewH);
                int trackRange = track.bottom - track.top;
                int thumbH = max((int)(30*d->dpi), viewH * viewH / max(1, d->sidebarContentHeight));
                int newY = y - d->scrollDragOffset;
                int newPos = maxScroll > 0 ? (newY - track.top) * maxScroll / trackRange : 0;
                if (newPos < 0) newPos = 0;
                if (newPos > maxScroll) newPos = maxScroll;
                d->sidebarScrollY = newPos;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if (d->scrollDragArea == 2) { // panel list
                RECT track = PanelListScrollTrackRect(d);
                int panelListViewH = d->rcPanelList.bottom - d->rcPanelList.top;
                int panelListContentH = d->numLayoutPanels * (int)(48 * d->dpi + 0.5f) + (int)(16 * d->dpi + 0.5f);
                int maxScroll = max(0, panelListContentH - panelListViewH);
                int trackRange = track.bottom - track.top;
                int thumbH = max((int)(30*d->dpi), panelListViewH * panelListViewH / max(1, panelListContentH));
                int newY = y - d->scrollDragOffset;
                int newPos = maxScroll > 0 ? (newY - track.top) * maxScroll / trackRange : 0;
                if (newPos < 0) newPos = 0;
                if (newPos > maxScroll) newPos = maxScroll;
                d->panelListScrollY = newPos;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        
        d->hoverCloseBtn = (PtInRect(&d->rcCloseBtn, { x, y }) != 0);
        d->hoverTab = -1;
        for (int i = 0; i < TAB_COUNT; ++i) if (PtInRect(&d->rcTabs[i], { x, y })) { d->hoverTab = i; break; }
        d->hoverSlot = -1;
        d->hoverBank = -1;
        d->hoverSidebar = -1;
        d->hoverPanel = -1;

        // Per-tab hover
        if (d->curTab == TAB_MEDIA) {
            d->hoverSlot = SlotAtPoint(d, x, y);
            for (int i = 0; i < NUM_BANKS; ++i) if (PtInRect(&d->rcBanks[i], { x, y })) { d->hoverBank = i; break; }
            std::vector<SbButton> btns;
            BuildSidebar(d, btns);
            for (auto& b : btns) if (PtInRect(&b.rc, { x, y })) { d->hoverSidebar = b.id; break; }

            if (d->slotClickPending && !d->slotMovePending) {
                int dx = abs(x - d->slotClickStart.x);
                int dy = abs(y - d->slotClickStart.y);
                if (dx >= GetSystemMetrics(SM_CXDRAG) || dy >= GetSystemMetrics(SM_CYDRAG)) {
                    KillTimer(hwnd, 3);
                    BeginSlotMove(d, d->slotClickBank, d->slotClickSlot);
                }
            }
        } else if (d->curTab == TAB_OPTIONS) {
            std::vector<SbButton> btns;
            BuildOptionsControls(d, btns);
            for (auto& b : btns) if (PtInRect(&b.rc, { x, y })) { d->hoverSidebar = b.id; break; }
        } else if (d->curTab == TAB_LICENSE) {
            std::vector<SbButton> btns;
            BuildLicenseControls(d, btns);
            for (auto& b : btns) if (PtInRect(&b.rc, { x, y })) { d->hoverSidebar = b.id; break; }
        } else if (d->curTab == TAB_LAYOUT) {
            // Layout panel hover + drag
            d->hoverPanel = PanelAtPoint(d, x, y);
            if (d->dragPanel >= 0 && d->dragMode != 0) {
                int dx = x - d->dragStart.x;
                int dy = y - d->dragStart.y;
                int cw = d->rcCanvas.right - d->rcCanvas.left;
                int ch = d->rcCanvas.bottom - d->rcCanvas.top;
                OverlayPanel& p = d->layoutPanels[d->dragPanel];
                if (d->dragMode == 1) { // move
                    p.x = d->dragOrigX + (float)dx / cw;
                    p.y = d->dragOrigY + (float)dy / ch;
                    if (p.x < 0) p.x = 0; if (p.y < 0) p.y = 0;
                    if (p.x + p.w > 1) p.x = 1 - p.w; if (p.y + p.h > 1) p.y = 1 - p.h;
                } else if (d->dragMode >= 2) {
                    // Resize from a specific corner
                    // dragMode: 2=TL 3=TR 4=BL 5=BR
                    float fdx = (float)dx / cw, fdy = (float)dy / ch;
                    bool leftEdge  = (d->dragMode == 2 || d->dragMode == 4);
                    bool topEdge   = (d->dragMode == 2 || d->dragMode == 3);
                    if (leftEdge) {
                        float newX = d->dragOrigX + fdx;
                        float newW = d->dragOrigW - fdx;
                        if (newW >= 0.05f) { p.x = newX; p.w = newW; }
                    } else {
                        p.w = d->dragOrigW + fdx;
                        if (p.w < 0.05f) p.w = 0.05f;
                        if (p.x + p.w > 1) p.w = 1 - p.x;
                    }
                    if (topEdge) {
                        float newY = d->dragOrigY + fdy;
                        float newH = d->dragOrigH - fdy;
                        if (newH >= 0.05f) { p.y = newY; p.h = newH; }
                    } else {
                        p.h = d->dragOrigH + fdy;
                        if (p.h < 0.05f) p.h = 0.05f;
                        if (p.y + p.h > 1) p.h = 1 - p.y;
                    }
                }
                // Live sync to plugin so the output updates while dragging/resizing
                if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
            // Layout sidebar hover (account for both sidebar and panel list scroll offsets)
            std::vector<SbButton> btns;
            BuildLayoutSidebar(d, btns);
            for (auto& b : btns) {
                RECT rc = b.rc;
                bool isPanelItem = (b.id >= LB_PANEL_SELECT_BASE && b.id < LB_PANEL_SELECT_BASE + MAX_OVERLAY_PANELS);
                // Apply sidebar scroll to all
                OffsetRect(&rc, 0, -d->sidebarScrollY);
                // Apply panel list scroll to panel items
                if (isPanelItem) {
                    OffsetRect(&rc, 0, -d->panelListScrollY);
                }
                if (PtInRect(&rc, { x, y })) { d->hoverSidebar = b.id; break; }
            }
            // Also check shader switcher row under canvas (no scroll offsets)
            if (d->hoverSidebar == -1) {
                std::vector<SbButton> shaderBtns;
                BuildLayoutShaderRowUnderCanvas(d, shaderBtns);
                for (auto& sb : shaderBtns) {
                    if (PtInRect(&sb.rc, { x, y })) { d->hoverSidebar = sb.id; break; }
                }
            }
        }

        if (!d->tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme); d->tracking = true;
        }
        if (oldSlot != d->hoverSlot || oldBank != d->hoverBank || oldTab != d->hoverTab ||
            oldSb != d->hoverSidebar || oldPanel != d->hoverPanel || (d->dragPanel >= 0 && d->dragMode != 0) ||
            oldHoverClose != d->hoverCloseBtn)
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        d->tracking = false;
        d->hoverSlot = d->hoverBank = d->hoverTab = d->hoverSidebar = d->hoverPanel = -1;
        d->hoverCloseBtn = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        if (d) {
            // Only cancel a slot MOVE drag here. A pending slot CLICK must survive
            // the ReleaseCapture() in WM_LBUTTONUP so timer 3 can fire ActivateSlot.
            if (d->slotMovePending) {
                CancelSlotInteraction(d);
            }
            d->dragPanel = -1;
            d->dragMode = 0;
            d->scrollDragging = false;
            d->scrollDragArea = 0;
            d->scrollDragOffset = 0;
        }
        return 0;
    case WM_RBUTTONDOWN: {
        break;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        
        // Check scrollbar clicks first
        if (d->curTab == TAB_MEDIA) {
            RECT gridThumb = GridScrollThumbRect(d);
            RECT gridTrack = GridScrollTrackRect(d);
            if (PtInRect(&gridThumb, { x, y })) {
                SetCapture(hwnd);
                d->scrollDragging = true;
                d->scrollDragArea = 0; // grid
                d->scrollDragOffset = y - gridThumb.top;
                return 0;
            } else if (PtInRect(&gridTrack, { x, y })) {
                // Click on track - jump to position
                int viewH = d->rcGrid.bottom - d->rcGrid.top;
                int maxScroll = max(0, d->contentHeight - viewH);
                int trackRange = gridTrack.bottom - gridTrack.top;
                int thumbH = max((int)(30*d->dpi), viewH * viewH / max(1, d->contentHeight));
                int newPos = maxScroll > 0 ? (y - gridTrack.top - thumbH/2) * maxScroll / trackRange : 0;
                if (newPos < 0) newPos = 0;
                if (newPos > maxScroll) newPos = maxScroll;
                d->scrollY = newPos;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        
        if (d->curTab == TAB_LAYOUT) {
            RECT panelListThumb = PanelListScrollThumbRect(d);
            RECT panelListTrack = PanelListScrollTrackRect(d);
            if (PtInRect(&panelListThumb, { x, y })) {
                SetCapture(hwnd);
                d->scrollDragging = true;
                d->scrollDragArea = 2; // panel list
                d->scrollDragOffset = y - panelListThumb.top;
                return 0;
            } else if (PtInRect(&panelListTrack, { x, y })) {
                // Click on track - jump to position
                int panelListViewH = d->rcPanelList.bottom - d->rcPanelList.top;
                int panelListContentH = d->numLayoutPanels * (int)(48 * d->dpi + 0.5f) + (int)(16 * d->dpi + 0.5f);
                int maxScroll = max(0, panelListContentH - panelListViewH);
                int trackRange = panelListTrack.bottom - panelListTrack.top;
                int thumbH = max((int)(30*d->dpi), panelListViewH * panelListViewH / max(1, panelListContentH));
                int newPos = maxScroll > 0 ? (y - panelListTrack.top - thumbH/2) * maxScroll / trackRange : 0;
                if (newPos < 0) newPos = 0;
                if (newPos > maxScroll) newPos = maxScroll;
                d->panelListScrollY = newPos;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            
            RECT sidebarThumb = SidebarScrollThumbRect(d);
            RECT sidebarTrack = SidebarScrollTrackRect(d);
            if (PtInRect(&sidebarThumb, { x, y })) {
                SetCapture(hwnd);
                d->scrollDragging = true;
                d->scrollDragArea = 1; // sidebar
                d->scrollDragOffset = y - sidebarThumb.top;
                return 0;
            } else if (PtInRect(&sidebarTrack, { x, y })) {
                // Click on track - jump to position
                int viewH = d->rcSidebar.bottom - d->rcSidebar.top;
                int maxScroll = max(0, d->sidebarContentHeight - viewH);
                int trackRange = sidebarTrack.bottom - sidebarTrack.top;
                int thumbH = max((int)(30*d->dpi), viewH * viewH / max(1, d->sidebarContentHeight));
                int newPos = maxScroll > 0 ? (y - sidebarTrack.top - thumbH/2) * maxScroll / trackRange : 0;
                if (newPos < 0) newPos = 0;
                if (newPos > maxScroll) newPos = maxScroll;
                d->sidebarScrollY = newPos;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        
        // Close button
        if (PtInRect(&d->rcCloseBtn, { x, y })) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        // tabs (all tabs)
        for (int i = 0; i < TAB_COUNT; ++i) if (PtInRect(&d->rcTabs[i], { x, y })) {
            CancelSlotInteraction(d);
            d->curTab = i; ComputeLayout(d); InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }

        // Options tab handling
        if (d->curTab == TAB_OPTIONS) {
            std::vector<SbButton> btns;
            BuildOptionsControls(d, btns);
            for (auto& b : btns) if (PtInRect(&b.rc, { x, y })) {
                HandleOptionsClick(d, b.id);
                return 0;
            }
            return 0;
        }

        // License tab handling
        if (d->curTab == TAB_LICENSE) {
            std::vector<SbButton> btns;
            BuildLicenseControls(d, btns);
            for (auto& b : btns) if (PtInRect(&b.rc, { x, y })) {
                HandleLicenseClick(d, b.id);
                return 0;
            }
            return 0;
        }

        // Layout tab handling
        if (d->curTab == TAB_LAYOUT) {
            // First check shader row under canvas (no scroll offsets)
            {
                std::vector<SbButton> shaderBtns;
                BuildLayoutShaderRowUnderCanvas(d, shaderBtns);
                for (auto& sb : shaderBtns) {
                    if (PtInRect(&sb.rc, { x, y })) {
                        HandleLayoutSidebarClick(d, sb.id);
                        return 0;
                    }
                }
            }
            // Layout sidebar (account for both sidebar and panel list scroll offsets)
            std::vector<SbButton> btns;
            BuildLayoutSidebar(d, btns);
            for (auto& b : btns) {
                RECT rc = b.rc;
                bool isPanelItem = (b.id >= LB_PANEL_SELECT_BASE && b.id < LB_PANEL_SELECT_BASE + MAX_OVERLAY_PANELS);
                // Apply sidebar scroll to all
                OffsetRect(&rc, 0, -d->sidebarScrollY);
                // Apply panel list scroll to panel items
                if (isPanelItem) {
                    OffsetRect(&rc, 0, -d->panelListScrollY);
                }
                if (PtInRect(&rc, { x, y })) {
                    HandleLayoutSidebarClick(d, b.id);
                    return 0;
                }
            }
            // Panel selection/drag on canvas.
            // Priority order:
            //   1. Corner handles on the SELECTED panel (always, regardless of direct-select)
            //   2. If direct-select OFF: body of the SELECTED panel (even if buried)
            //   3. If direct-select ON:  top-most panel at cursor (normal click-to-select)

            int panelIdx = -1;
            int dragMode = 1;

            // Always check selected panel's corners first
            if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
                int corner = PanelCornerHit(d, d->layoutPanels[d->selectedPanel], x, y);
                if (corner >= 0) {
                    panelIdx = d->selectedPanel;
                    dragMode = 2 + corner; // 2=TL 3=TR 4=BL 5=BR
                }
            }

            // If no corner hit, resolve which panel body was clicked
            if (panelIdx < 0) {
                if (!d->directSelectEnabled) {
                    // List-select mode: only interact with the selected panel's body
                    if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
                        RECT pr = PanelRectPx(d, d->layoutPanels[d->selectedPanel]);
                        if (PtInRect(&pr, { x, y })) {
                            panelIdx = d->selectedPanel;
                            dragMode = 1;
                        }
                    }
                } else {
                    // Direct-select mode: pick top-most panel at cursor
                    panelIdx = PanelAtPoint(d, x, y);
                    dragMode = 1;
                }
            }

            if (panelIdx >= 0) {
                if (d->directSelectEnabled)
                    d->selectedPanel = panelIdx;
                d->dragPanel = panelIdx;
                d->dragMode  = dragMode;
                d->dragStart = { x, y };
                OverlayPanel& p = d->layoutPanels[panelIdx];
                d->dragOrigX = p.x; d->dragOrigY = p.y;
                d->dragOrigW = p.w; d->dragOrigH = p.h;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                if (d->directSelectEnabled) {
                    d->selectedPanel = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        // Media tab handling
        if (d->curTab == TAB_MEDIA) {
            // banks
            for (int i = 0; i < NUM_BANKS; ++i) if (PtInRect(&d->rcBanks[i], { x, y })) {
                CancelSlotInteraction(d);
                d->grid.curBank = i; d->grid.selectedSlot = -1; d->scrollY = 0;
                InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            // sidebar
            std::vector<SbButton> btns;
            BuildSidebar(d, btns);
            for (auto& b : btns) if (PtInRect(&b.rc, { x, y })) {
                CancelSlotInteraction(d);
                HandleSidebarClick(d, b.id);
                return 0;
            }
            // grid
            int slot = SlotAtPoint(d, x, y);
            if (slot >= 0) {
                if (d->slotMovePending) {
                    CommitSlotMove(d, d->grid.curBank, slot);
                    return 0;
                }
                d->slotClickPending = true;
                d->slotClickBank = d->grid.curBank;
                d->slotClickSlot = slot;
                d->slotClickStart = { x, y };
                d->grid.selectedSlot = slot;
                SetCapture(hwnd);
                KillTimer(hwnd, 3);
                SetTimer(hwnd, 3, GetDoubleClickTime(), nullptr);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                CancelSlotInteraction(d);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        // Handle slot move drop BEFORE releasing capture, since ReleaseCapture()
        // triggers WM_CAPTURECHANGED which cancels the pending slot move.
        if (d->curTab == TAB_MEDIA && d->slotMovePending) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int dstSlot = SlotAtPoint(d, x, y);
            int dstBank = (dstSlot >= 0) ? d->grid.curBank : BankAtPoint(d, x, y);
            if (dstBank < 0) {
                CancelSlotInteraction(d);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (dstSlot < 0) dstSlot = d->slotMoveSource;
            CommitSlotMove(d, dstBank, dstSlot);
            return 0;
        }

        if (d->curTab == TAB_MEDIA && d->slotClickPending && !d->slotMovePending) {
            KillTimer(hwnd, 3);
            ActivateSlot(d, d->slotClickBank, d->slotClickSlot);
            CancelSlotInteraction(d);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        // End scrollbar drag
        if (d->scrollDragging) {
            d->scrollDragging = false;
            d->scrollDragArea = 0;
            d->scrollDragOffset = 0;
            return 0;
        }
        if (d->dragPanel >= 0 && d->host)
            d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
        d->dragPanel = -1;
        d->dragMode = 0;
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        if (d->curTab == TAB_MEDIA) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int slot = SlotAtPoint(d, x, y);
            if (slot >= 0) {
                KillTimer(hwnd, 3);
                CancelSlotInteraction(d);
                d->grid.selectedSlot = slot;
                OpenFileForSlot(d, slot);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (d->curTab == TAB_LAYOUT) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int panelIdx = PanelAtPointForContext(d, x, y);
            if (panelIdx >= 0) {
                OverlayPanel& p = d->layoutPanels[panelIdx];
                d->selectedPanel = panelIdx;
                if (p.panelType == 1) {          // Image panel
                    LoadImageForPanel(d, panelIdx);
                } else if (p.panelType == 2) {   // Text panel
                    ShowTextPanelEditor(d, panelIdx);
                } else if (p.panelType == 4) {   // Song panel
                    ShowSongPanelEditor(d, panelIdx);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        if (d->curTab == TAB_MEDIA) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int slot = SlotAtPoint(d, x, y);
            if (slot >= 0) {
                if (d->slotMovePending) {
                    CommitSlotMove(d, d->grid.curBank, slot);
                    return 0;
                }
                CancelSlotInteraction(d);
                d->grid.selectedSlot = slot;
                ShowSlotContextMenu(d, slot, { x, y });
                return 0;
            }
        }
        if (d->curTab == TAB_LAYOUT) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int panelIdx = PanelAtPointForContext(d, x, y);
            if (panelIdx >= 0) {
                d->selectedPanel = panelIdx;
                InvalidateRect(hwnd, nullptr, FALSE);
                OverlayPanel& p = d->layoutPanels[panelIdx];

                // Build context menu
                HMENU hMenu = CreatePopupMenu();
                enum { CM_LOAD_IMAGE = 1, CM_REMOVE_IMAGE, CM_EDIT_TEXT, CM_EDIT_SONG, CM_DELETE_PANEL };
                if (p.panelType == 1) { // Image panel
                    AppendMenuW(hMenu, MF_STRING, CM_LOAD_IMAGE, L"Load Image...");
                    if (p.hasImage)
                        AppendMenuW(hMenu, MF_STRING, CM_REMOVE_IMAGE, L"Remove Image");
                } else if (p.panelType == 2) { // Text panel
                    AppendMenuW(hMenu, MF_STRING, CM_EDIT_TEXT, L"Edit Text & Style...");
                } else if (p.panelType == 4) { // Song panel
                    AppendMenuW(hMenu, MF_STRING, CM_EDIT_SONG, L"Edit Now Playing Style...");
                }
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, CM_DELETE_PANEL, L"Delete Panel");

                POINT pt = { x, y };
                ClientToScreen(hwnd, &pt);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                         pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(hMenu);

                switch (cmd) {
                case CM_LOAD_IMAGE:   LoadImageForPanel(d, panelIdx);   break;
                case CM_REMOVE_IMAGE: RemoveImageFromPanel(d, panelIdx); break;
                case CM_EDIT_TEXT: {
                    ShowTextPanelEditor(d, panelIdx);
                    break;
                }
                case CM_EDIT_SONG: {
                    ShowSongPanelEditor(d, panelIdx);
                    break;
                }
                case CM_DELETE_PANEL:
                    DeleteLayoutPanel(d, panelIdx);
                    break;
                }
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int scrollLines = delta / WHEEL_DELTA;
        if (d->curTab == TAB_LAYOUT) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            // Check if mouse is over panel list area (account for sidebar scroll)
            RECT panelListVisible = d->rcPanelList;
            OffsetRect(&panelListVisible, 0, -d->sidebarScrollY);
            if (PtInRect(&panelListVisible, pt)) {
                // Scroll panel list
                int panelListContentH = d->numLayoutPanels * ((int)(32 * d->dpi) + (int)(8 * d->dpi));
                int panelListViewH = d->rcPanelList.bottom - d->rcPanelList.top;
                int maxScroll = max(0, panelListContentH - panelListViewH);
                int lineH = (int)(32 * d->dpi);
                d->panelListScrollY -= scrollLines * lineH * 3;
                if (d->panelListScrollY < 0) d->panelListScrollY = 0;
                if (d->panelListScrollY > maxScroll) d->panelListScrollY = maxScroll;
            } else {
                // Scroll full sidebar
                int viewH = d->rcSidebar.bottom - d->rcSidebar.top;
                int maxScroll = max(0, d->sidebarContentHeight - viewH);
                int lineH = (int)(32 * d->dpi);
                d->sidebarScrollY -= scrollLines * lineH * 3;
                if (d->sidebarScrollY < 0) d->sidebarScrollY = 0;
                if (d->sidebarScrollY > maxScroll) d->sidebarScrollY = maxScroll;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (d->curTab == TAB_MEDIA) {
            // Scroll grid
            int viewH = d->rcGrid.bottom - d->rcGrid.top;
            int maxScroll = max(0, d->contentHeight - viewH);
            int lineH = (int)(32 * d->dpi);
            d->scrollY -= scrollLines * lineH * 3;
            if (d->scrollY < 0) d->scrollY = 0;
            if (d->scrollY > maxScroll) d->scrollY = maxScroll;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && d->slotMovePending) {
            KillTimer(hwnd, 3);
            CancelSlotInteraction(d);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        } else if (wp == VK_ESCAPE && d->slotClickPending) {
            KillTimer(hwnd, 3);
            CancelSlotInteraction(d);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        } else if (wp == VK_DELETE && d->grid.selectedSlot >= 0) {
            ClearSlot(d, d->grid.curBank, d->grid.selectedSlot);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == '1') { CancelSlotMove(d); d->curTab = TAB_MEDIA; ComputeLayout(d); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wp == '2') { CancelSlotMove(d); d->curTab = TAB_LAYOUT; ComputeLayout(d); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wp == '3') { CancelSlotMove(d); d->curTab = TAB_OPTIONS; ComputeLayout(d); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wp == '4') { CancelSlotMove(d); d->curTab = TAB_LICENSE; ComputeLayout(d); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_CLOSE:
        if (d) {
            CancelSlotInteraction(d);
            if (d->host) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        POINT pt; DragQueryPoint(drop, &pt);
        int n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        int startSlot = SlotAtPoint(d, pt.x, pt.y);
        if (startSlot < 0) startSlot = d->grid.selectedSlot >= 0 ? d->grid.selectedSlot : 0;
        for (int i = 0; i < n && startSlot + i < SLOTS_PER_BANK; ++i) {
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(drop, i, path, MAX_PATH))
                AssignSlot(d, d->grid.curBank, startSlot + i, path);
        }
        d->grid.selectedSlot = startSlot;
        DragFinish(drop);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1 && d->sidebar.ssRunning) {
            KillTimer(hwnd, 1);
            DoSlideshowStep(d);
            SetTimer(hwnd, 1, d->sidebar.ssDelaySec * 1000, nullptr);
        } else if (wp == 3) {
            if (d->slotClickPending && !d->slotMovePending) {
                if (GetKeyState(VK_LBUTTON) < 0) {
                    KillTimer(hwnd, 3);
                    SetTimer(hwnd, 3, 50, nullptr);
                } else {
                    KillTimer(hwnd, 3);
                    ActivateSlot(d, d->slotClickBank, d->slotClickSlot);
                    CancelSlotInteraction(d);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else {
                KillTimer(hwnd, 3);
            }
        } else if (wp == 2) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        TmUI::SaveState(d);
        SaveBankConfig(d);
        SaveKaraokePrefs(d);
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        CancelSlotInteraction(d);
        for (int i = 0; i < MAX_OVERLAY_PANELS; ++i) ClearPanelImageCache(d, i);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

namespace TmUI {

static void RegisterAndInit(HINSTANCE hInst)
{
    if (g_gdiRef++ == 0) {
        GdiplusStartupInput gsi;
        GdiplusStartup(&g_gdiToken, &gsi, nullptr);
    }
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

static TmUIData* NewData(HINSTANCE hInst, ITmUIHost* host)
{
    TmUIData* d = new TmUIData();
    ZeroMemory(d, sizeof(TmUIData));
    d->hInst = hInst;
    d->host = host;
    d->curTab = TAB_MEDIA;
    d->grid.curBank = 0;
    d->grid.selectedSlot = -1;
    d->hoverSlot = d->hoverBank = d->hoverTab = d->hoverSidebar = -1;
    d->slotClickPending = false;
    d->slotClickBank = -1;
    d->slotClickSlot = -1;
    d->slotClickStart = { 0, 0 };
    d->slotMoveSourceBank = -1;
    d->slotMovePending = false;
    d->slotMoveSource = -1;
    d->scrollDragging = false;
    d->scrollDragArea = 0;
    d->scrollDragOffset = 0;
    d->numLayoutPanels = 0;
    d->selectedPanel = d->hoverPanel = d->dragPanel = -1;
    d->dragMode = 0;
    d->directSelectEnabled = true;
    d->sidebar.scaleMode = SCALE_ASPECT;
    d->sidebar.ssEnabled = true;  // Slideshow enabled by default
    d->sidebar.ssDelaySec = 5;
    d->sidebar.ssTransition = 1;
    d->sidebar.ssDirection = 0;
    d->sidebar.ssLoop = true;
    d->sidebar.renderMode = TM_RENDER_AUTO;
    d->karaokeAutoHide = true;
    LoadState(d);
    LoadKaraokePrefs(d);
    for (int i = 0; i < NUM_BANKS; ++i) {
        if (d->grid.bankColors[i] == 0) d->grid.bankColors[i] = DefaultBankColor(i);
    }
    LoadBankConfig(d);
    if (d->sidebar.renderMode < TM_RENDER_AUTO || d->sidebar.renderMode > TM_RENDER_GPU) d->sidebar.renderMode = TM_RENDER_AUTO;
    if (d->host) d->host->HostRenderMode(d->sidebar.renderMode);
    if (d->host) d->host->HostTransitionMode(d->sidebar.ssTransition);
    if (d->host) d->host->HostSetKaraokeAutoHide(d->karaokeAutoHide);

    // Add main panel by default if no panels exist (after LoadState)
    if (d->numLayoutPanels == 0) {
        TM_INFO("Initializing default main panel");
        OverlayPanel& p = d->layoutPanels[0];
        ZeroMemory(&p, sizeof(OverlayPanel));
        p.panelType = 0; // Main slideshow
        p.x = 0; p.y = 0; p.w = 1; p.h = 1; // Full screen
        p.visible = true;
        p.bgOpacity = 0; // Transparent
        p.textColor = RGB(255, 255, 255);
        p.textAlpha = 255;
        p.textAlpha2 = 255;
        p.textColor2 = p.textColor;
        p.bgColor = RGB(0, 0, 0);
        p.fontSize = 32;
        wcscpy_s(p.fontName, L"Segoe UI");
        wcscpy_s(p.headingFontName, p.fontName);
        p.headingTextColor = p.textColor;
        p.headingTextColor2 = p.headingTextColor;
        p.headingTextAlpha = 255;
        p.headingTextAlpha2 = 255;
        wcscpy_s(p.songFontName, p.fontName);
        p.songTextColor = p.textColor;
        p.songTextColor2 = p.songTextColor;
        p.songTextAlpha = 255;
        p.songTextAlpha2 = 255;
        p.zOrder = 0;
        d->numLayoutPanels = 1;
        d->selectedPanel = 0;
    } else {
        TM_INFO("Loaded %d panels from state", d->numLayoutPanels);
    }
    // Sync panels to the plugin so it starts with the configured main panel
    // instead of falling back to the no-panel fullscreen path.
    if (d->host) d->host->UISetOverlayPanels(d->layoutPanels, d->numLayoutPanels);
    TM_INFO("NewData complete: numLayoutPanels=%d selectedPanel=%d", d->numLayoutPanels, d->selectedPanel);
    return d;
}

HWND Create(HINSTANCE hInst, ITmUIHost* host, HWND parent)
{
    RegisterAndInit(hInst);
    TmUIData* d = NewData(hInst, host);

    int w = 1280, h = 820;
    DWORD style = parent ? (WS_CHILD | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    return CreateWindowExW(0, kClassName, L"TellyMedia v4",
        style, CW_USEDEFAULT, CW_USEDEFAULT, w, h, parent, nullptr, hInst, d);
}

HWND CreateEmbedded(HINSTANCE hInst, ITmUIHost* host)
{
    RegisterAndInit(hInst);
    TmUIData* d = NewData(hInst, host);

    // Top-level popup with a resizable frame. VirtualDJ shows this as the
    // plugin's settings dialog (VDJINTERFACE_DIALOG) and manages visibility.
    // Must NOT be WS_CHILD and must have a null parent (matches v2).
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    return CreateWindowExW(WS_EX_COMPOSITED, kClassName, L"TellyMedia v4",
        style, 0, 0, 1280, 820, nullptr, nullptr, hInst, d);
}

void Destroy(HWND hWnd)
{
    TmUIData* d = (TmUIData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (d) {
        for (int b = 0; b < NUM_BANKS; ++b)
            for (int i = 0; i < SLOTS_PER_BANK; ++i)
                if (d->grid.banks[b][i].hThumb) DeleteObject(d->grid.banks[b][i].hThumb);
        d->fonts.Destroy();
        delete d;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
    }
    if (--g_gdiRef == 0 && g_gdiToken) {
        if (g_logoImage) { delete g_logoImage; g_logoImage = nullptr; }
        GdiplusShutdown(g_gdiToken); g_gdiToken = 0;
    }
}

// Persist only essential data (paths/names/settings). Thumbs regenerated on load.
void SaveState(TmUIData* d)
{
    wchar_t path[MAX_PATH]; StatePath(path, MAX_PATH);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    DWORD magic = 0x544D5634; // 'TMV4'
    WriteFile(f, &magic, sizeof(magic), &wr, nullptr);
    WriteFile(f, &d->grid.curBank, sizeof(int), &wr, nullptr);
    WriteFile(f, &d->sidebar, sizeof(SidebarSettings), &wr, nullptr);
    WriteFile(f, d->grid.bankNames, sizeof(d->grid.bankNames), &wr, nullptr);
    WriteFile(f, &d->numLayoutPanels, sizeof(int), &wr, nullptr);
    WriteFile(f, d->layoutPanels, sizeof(OverlayPanel) * d->numLayoutPanels, &wr, nullptr);
    for (int b = 0; b < NUM_BANKS; ++b)
        for (int i = 0; i < SLOTS_PER_BANK; ++i) {
            SlotData& s = d->grid.banks[b][i];
            BYTE has = s.hasFile ? 1 : 0;
            WriteFile(f, &has, 1, &wr, nullptr);
            if (has) {
                WriteFile(f, s.filePath, sizeof(s.filePath), &wr, nullptr);
                WriteFile(f, &s.scaleMode, sizeof(int), &wr, nullptr);
                WriteFile(f, &s.rotation, sizeof(int), &wr, nullptr);
                WriteFile(f, &s.builtinFx, sizeof(int), &wr, nullptr);
            }
        }
    CloseHandle(f);
}

void LoadState(TmUIData* d)
{
    wchar_t path[MAX_PATH]; StatePath(path, MAX_PATH);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD rd; DWORD magic = 0;
    ReadFile(f, &magic, sizeof(magic), &rd, nullptr);
    if (magic != 0x544D5634) { CloseHandle(f); return; }
    ReadFile(f, &d->grid.curBank, sizeof(int), &rd, nullptr);
    ReadFile(f, &d->sidebar, sizeof(SidebarSettings), &rd, nullptr);
    ReadFile(f, d->grid.bankNames, sizeof(d->grid.bankNames), &rd, nullptr);

    // Sanitize sidebar settings (guard against stale/garbage values).
    if (d->sidebar.scaleMode < 0 || d->sidebar.scaleMode > 2) d->sidebar.scaleMode = SCALE_ASPECT;
    if (d->sidebar.ssTransition < 0 || d->sidebar.ssTransition > 2) d->sidebar.ssTransition = 1;
    if (d->sidebar.ssDirection < 0 || d->sidebar.ssDirection > 2) d->sidebar.ssDirection = 0;
    if (d->sidebar.ssDelaySec < 1 || d->sidebar.ssDelaySec > 60) d->sidebar.ssDelaySec = 5;
    if (d->sidebar.renderMode < TM_RENDER_AUTO || d->sidebar.renderMode > TM_RENDER_GPU) d->sidebar.renderMode = TM_RENDER_AUTO;
    if (d->sidebar.mainShaderIndex < 0) d->sidebar.mainShaderIndex = 0;

    d->numLayoutPanels = 0;
    int savedPanels = 0;
    if (ReadFile(f, &savedPanels, sizeof(int), &rd, nullptr) && rd == sizeof(int)) {
        if (savedPanels < 0) savedPanels = 0;
        if (savedPanels > MAX_OVERLAY_PANELS) savedPanels = MAX_OVERLAY_PANELS;
        for (int i = 0; i < savedPanels; ++i) {
            OverlayPanel panel = {};
            if (!ReadFile(f, &panel, sizeof(OverlayPanel), &rd, nullptr) || rd != sizeof(OverlayPanel)) {
                savedPanels = i;
                break;
            }
            d->layoutPanels[i] = panel;
            d->layoutPanels[i].visible = panel.visible;
            d->layoutPanels[i].hasImage = panel.hasImage && panel.imagePath[0] != L'\0';
            d->layoutPanels[i].zOrder = panel.zOrder;
        }
        d->numLayoutPanels = savedPanels;
        if (d->numLayoutPanels > 0) d->selectedPanel = 0;
    }
    for (int b = 0; b < NUM_BANKS; ++b)
        for (int i = 0; i < SLOTS_PER_BANK; ++i) {
            BYTE has = 0;
            if (!ReadFile(f, &has, 1, &rd, nullptr) || rd == 0) { CloseHandle(f); return; }
            if (has) {
                SlotData& s = d->grid.banks[b][i];
                ReadFile(f, s.filePath, sizeof(s.filePath), &rd, nullptr);
                ReadFile(f, &s.scaleMode, sizeof(int), &rd, nullptr);
                ReadFile(f, &s.rotation, sizeof(int), &rd, nullptr);
                ReadFile(f, &s.builtinFx, sizeof(int), &rd, nullptr);
                // rebuild metadata + thumbnail
                const wchar_t* name = wcsrchr(s.filePath, L'\\');
                name = name ? name + 1 : s.filePath;
                wcsncpy_s(s.displayName, name, _TRUNCATE);
                s.hasFile = (GetFileAttributesW(s.filePath) != INVALID_FILE_ATTRIBUTES);
                if (s.hasFile) s.hThumb = MakeThumbnail(s.filePath, 128);
            }
        }
    CloseHandle(f);
}

} // namespace TmUI
