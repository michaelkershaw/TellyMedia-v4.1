#pragma once
#include "tm/TmPlatform.h"

// ─── Plugin version (single source of truth) ─────────────────────────────────
#define TELLYMEDIA_VERSION   "4.0.0"
#define TELLYMEDIA_VERSION_W L"4.0.0"

// ─── Parameter IDs exposed to VirtualDJ ──────────────────────────────────────
enum TmParam {
    PARAM_OPEN_FILE  = 1,
    PARAM_PLAY       = 2,
    PARAM_STOP       = 3,
    PARAM_PAUSE      = 4,
    PARAM_VOLUME     = 5,
    PARAM_LOOP       = 6,
    PARAM_ALPHA      = 7,
    PARAM_SEND_CLICK = 8,
    PARAM_SEND_TEXT  = 9,
    PARAM_SMS_THEME  = 10
};

// ─── Bank / grid constants ───────────────────────────────────────────────────
#define NUM_BANKS       14
#define SLOTS_PER_BANK  200
#define GRID_COLS       7
#define GRID_ROWS       29

// ─── Scale modes ─────────────────────────────────────────────────────────────
enum TmScaleMode { SCALE_ASPECT = 0, SCALE_STRETCH = 1, SCALE_NOSCALE = 2 };

// ─── Built-in image effects ──────────────────────────────────────────────────
enum TmFx {
    FX_NONE = 0, FX_GRAYSCALE, FX_SEPIA, FX_INVERT,
    FX_TINT_RED, FX_TINT_GREEN, FX_TINT_BLUE,
    FX_BRIGHTNESS, FX_VIGNETTE, FX_BLUR, FX_COUNT
};

// ─── Overlay panel (normalized 0..1 rect over the output frame) ──────────────
struct OverlayPanel {
    float   x, y, w, h;
    wchar_t imagePath[MAX_PATH];
    bool    hasImage;
    bool    visible;
    int     panelType;          // 0=main slideshow, 1=image, 2=text, 3=shader, 4=song/now playing
    wchar_t textContent[512];
    int     textAnimMode;       // 0=Static 1=Scroll Left 2=Scroll Right 3=Bounce 4=Zoom In/Out 5=Typewriter
    int     imageAnimMode;      // 0=Static 1=Rotate 2=Pulse 3=ZoomIn 4=ZoomOut ...
    int     imageFitMode;       // 0=Contain (no crop), 1=Cover (fill, may crop)
    int     imageSafeMargin;    // Safety margin percent (0-20) to keep inside during anim
    int     fontSize;
    wchar_t fontName[64];
    COLORREF textColor;
    COLORREF textColor2;
    BYTE    textAlpha;
    BYTE    textAlpha2;
    COLORREF bgColor;
    bool    hasBgColor;
    COLORREF outlineColor;
    int     outlineWidth;
    int     bgOpacity;
    int     zOrder;
    int     shaderIndex;
    bool    smsBubbleStyle;
    wchar_t senderName[64];
    wchar_t timestamp[32];
    int     smsBubbleTheme;
    bool    rainbowColor;       // Enable rainbow color cycling for text
    int     songAnimMode;       // 0=Static 1=Pulse 2=Wave 3=Glitch 4=Neon 5=Bounce 6=Scroll 7=Scroll1Line
    int     textAlign;          // 0=Left, 1=Center, 2=Right
    wchar_t headingFontName[64];
    COLORREF headingTextColor;
    COLORREF headingTextColor2;
    BYTE    headingTextAlpha;
    BYTE    headingTextAlpha2;
    wchar_t songFontName[64];
    COLORREF songTextColor;
    COLORREF songTextColor2;
    BYTE    songTextAlpha;
    BYTE    songTextAlpha2;
    
    // Enhanced now playing panel options
    int     fontWeight;         // 0=Regular, 1=Bold, 2=Light, 3=Italic
    int     fontStyle;          // 0=Normal, 1=Italic, 2=Underline
    int     bgStyle;            // 0=Solid, 1=Gradient, 2=Transparent, 3=Glass/Blur
    COLORREF bgColor2;          // Second color for gradient
    int     cornerRadius;       // Rounded corner radius (0=sharp)
    int     shadowBlur;         // Shadow blur amount
    COLORREF shadowColor;       // Shadow color
    int     layoutMode;         // 0=Minimal, 1=Detailed, 2=Compact, 3=Full
    bool    showTitle;          // Show track title
    bool    showArtist;         // Show artist name
    bool    showAlbum;          // Show album name
    bool    showBPM;            // Show BPM
    bool    showTime;           // Show time remaining
    bool    showProgress;       // Show progress bar
    COLORREF progressColor;      // Progress bar color
    COLORREF progressBgColor;   // Progress bar background color
    float   scrollSpeed;        // Scroll animation speed (pixels/second)
};

static const int MAX_OVERLAY_PANELS = 20;

// ─── Media slot ──────────────────────────────────────────────────────────────
struct SlotData {
    wchar_t filePath[MAX_PATH];
    wchar_t displayName[48];
    char    fileExt[8];
    HBITMAP hThumb;
    bool    hasFile;
    int     scaleMode;
    int     rotation;
    int     builtinFx;
    bool    hasCustomPanels;
    int     numCustomPanels;
    OverlayPanel* customPanels;
};

// ─── Grid state ──────────────────────────────────────────────────────────────
struct GridState {
    SlotData banks[NUM_BANKS][SLOTS_PER_BANK];
    int      curBank;
    int      selectedSlot;
    wchar_t  bankNames[NUM_BANKS][32];
    COLORREF bankColors[NUM_BANKS];
};

// ─── ClickSend / SMS settings ────────────────────────────────────────────────
struct ClickSendSettings {
    wchar_t  fontName[64];
    int      fontSize;
    COLORREF textColor;
    COLORREF bgColor;
    bool     hasBgColor;
    int      bgOpacity;
    COLORREF outlineColor;
    int      outlineWidth;
    int      textAnimMode;
    float    x, y, w, h;
    char     apiUsername[128];
    char     apiKey[128];
    char     phoneNumber[32];
    bool     enableSMS;
    wchar_t  senderName[64];
    bool     useBubbleStyle;
    bool     enableInbound;
    int      pollIntervalSec;
    char     allowedSender[32];
    int      smsLayoutMode;
    int      smsMaxMessages;
};

// ─── Licensing state ─────────────────────────────────────────────────────────
enum LicenseStatus { LIC_UNCHECKED, LIC_VALID, LIC_EXPIRED, LIC_INVALID, LIC_REVOKED, LIC_ERROR };

struct LicenseState {
    LicenseStatus status;
    wchar_t  userEmail[256];
    char     licenseKey[64];
    char     authToken[512];
    char     licExpiry[24];
    char     tokenExpiry[24];
    char     licenseType[32];
    char     message[128];
    char     sharedOwnerName[128];
    char     sharedOwnerEmail[128];
    char     sharedDate[24];
    int      pluginId;
    int      maxActivations;
    int      currentActivations;
    int      maxSubLicenses;
    int      currentSubLicenses;
    bool     savePassword;
    bool     isSharedLicense;
    bool     licensed;
};

inline const char* LicenseStatusToString(LicenseStatus s) {
    switch (s) {
    case LIC_UNCHECKED: return "LIC_UNCHECKED";
    case LIC_VALID:     return "LIC_VALID";
    case LIC_EXPIRED:   return "LIC_EXPIRED";
    case LIC_INVALID:   return "LIC_INVALID";
    case LIC_REVOKED:   return "LIC_REVOKED";
    case LIC_ERROR:     return "LIC_ERROR";
    default:            return "LIC_UNCHECKED";
    }
}
