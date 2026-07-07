// TmWebView.cpp — Shared state serialization and command dispatch
// Used by both WKWebView (macOS) and WebView2 (Windows) hosts.
// The C++ side owns all TmUIData state. This file serializes it to JSON
// for the JS frontend and dispatches commands received from JS.

#include "tm/TmPlatform.h"
#include "tm/TmWebView.h"
#include "tm/TmUI.h"
#include "tm/TmMedia.h"
#include "tm/TmLogger.h"
#include <string>
#include <sstream>
#include <cstring>

// ─── JSON string escaping ────────────────────────────────────────────────────
static std::string JsonEscape(const wchar_t* s) {
    if (!s) return "";
    std::string out;
    // Convert wchar_t to UTF-8, then escape JSON special chars
    // Simple approach: convert char by char (ASCII subset is fine for most names)
    for (const wchar_t* p = s; *p; p++) {
        if (*p < 128) {
            char c = (char)*p;
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        } else {
            // UTF-8 encode
            if (*p < 0x800) {
                out += (char)(0xC0 | (*p >> 6));
                out += (char)(0x80 | (*p & 0x3F));
            } else {
                out += (char)(0xE0 | (*p >> 12));
                out += (char)(0x80 | ((*p >> 6) & 0x3F));
                out += (char)(0x80 | (*p & 0x3F));
            }
        }
    }
    return out;
}

static std::string JsonEscape(const char* s) {
    if (!s) return "";
    std::string out;
    for (const char* p = s; *p; p++) {
        if (*p == '"') out += "\\\"";
        else if (*p == '\\') out += "\\\\";
        else if (*p == '\n') out += "\\n";
        else if (*p == '\r') out += "\\r";
        else if (*p == '\t') out += "\\t";
        else out += *p;
    }
    return out;
}

static std::string ColorToJson(COLORREF c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X",
             (int)GetRValue(c), (int)GetGValue(c), (int)GetBValue(c));
    return std::string(buf);
}

// ─── State serialization ─────────────────────────────────────────────────────
namespace TmWebView {

std::string SerializeState(TmUIData* d) {
    if (!d) return "{}";
    std::ostringstream j;
    j << "{";

    // curTab
    j << "\"curTab\":" << d->curTab;

    // karaokeAutoHide
    j << ",\"karaokeAutoHide\":" << (d->karaokeAutoHide ? "true" : "false");

    // gridCols
    j << ",\"gridCols\":" << d->metrics.gridCols;

    // ── Grid state ──
    j << ",\"grid\":{";
    j << "\"curBank\":" << d->grid.curBank;
    j << ",\"banks\":[";
    for (int b = 0; b < NUM_BANKS; b++) {
        if (b > 0) j << ",";
        j << "{";
        j << "\"name\":\"" << JsonEscape(d->grid.bankNames[b]) << "\"";
        j << ",\"color\":\"" << ColorToJson(d->grid.bankColors[b]) << "\"";
        j << ",\"slots\":[";
        for (int s = 0; s < SLOTS_PER_BANK; s++) {
            if (s > 0) j << ",";
            const SlotData& slot = d->grid.banks[b][s];
            j << "{";
            j << "\"hasFile\":" << (slot.hasFile ? "true" : "false");
            j << ",\"displayName\":\"" << JsonEscape(slot.displayName) << "\"";
            j << ",\"isVideo\":" << (TmMedia::IsVideoPath(slot.filePath) ? "true" : "false");
            j << ",\"selected\":" << (b == d->grid.curBank && s == d->grid.selectedSlot ? "true" : "false");
            j << ",\"moveSource\":" << (d->slotMovePending && b == d->slotMoveSourceBank && s == d->slotMoveSource ? "true" : "false");
            // Thumbnail is sent separately via PushThumbnail for efficiency
            j << "}";
        }
        j << "]}";
    }
    j << "]}";

    // ── Sidebar settings ──
    j << ",\"sidebar\":{";
    j << "\"playing\":" << (d->sidebar.playing ? "true" : "false");
    j << ",\"scaleMode\":" << d->sidebar.scaleMode;
    j << ",\"ssEnabled\":" << (d->sidebar.ssEnabled ? "true" : "false");
    j << ",\"ssRunning\":" << (d->sidebar.ssRunning ? "true" : "false");
    j << ",\"ssDelaySec\":" << d->sidebar.ssDelaySec;
    j << ",\"ssTransition\":" << d->sidebar.ssTransition;
    j << ",\"ssDirection\":" << d->sidebar.ssDirection;
    j << ",\"ssLoop\":" << (d->sidebar.ssLoop ? "true" : "false");
    j << ",\"renderMode\":" << d->sidebar.renderMode;
    j << ",\"shadersEnabled\":" << (d->sidebar.shadersEnabled ? "true" : "false");
    j << ",\"shaderDisableOnKaraoke\":" << (d->sidebar.shaderDisableOnKaraoke ? "true" : "false");
    j << ",\"mainShaderIndex\":" << d->sidebar.mainShaderIndex;
    j << "}";

    // ── Layout panels ──
    j << ",\"layout\":{";
    j << "\"selectedPanel\":" << d->selectedPanel;
    j << ",\"directSelectEnabled\":" << (d->directSelectEnabled ? "true" : "false");
    j << ",\"panels\":[";
    for (int i = 0; i < d->numLayoutPanels; i++) {
        if (i > 0) j << ",";
        const OverlayPanel& p = d->layoutPanels[i];
        j << "{";
        j << "\"x\":" << p.x << ",\"y\":" << p.y << ",\"w\":" << p.w << ",\"h\":" << p.h;
        j << ",\"panelType\":" << p.panelType;
        j << ",\"visible\":" << (p.visible ? "true" : "false");
        j << ",\"hasImage\":" << (p.hasImage ? "true" : "false");
        j << ",\"textContent\":\"" << JsonEscape(p.textContent) << "\"";
        j << ",\"shaderIndex\":" << p.shaderIndex;
        j << ",\"imageFitMode\":" << p.imageFitMode;
        j << ",\"zOrder\":" << p.zOrder;
        j << "}";
    }
    j << "]}";

    // ── Shaders list ──
    // (Populated by the plugin when shaders are loaded)
    j << ",\"shaders\":[]";

    // ── License ──
    if (d->host) {
        LicenseState lic;
        if (d->host->HostGetLicenseState(&lic)) {
            j << ",\"license\":{";
            j << "\"status\":\"" << LicenseStatusToString(lic.status) << "\"";
            j << ",\"licensed\":" << (lic.licensed ? "true" : "false");
            j << ",\"userEmail\":\"" << JsonEscape(lic.userEmail) << "\"";
            j << ",\"licenseType\":\"" << JsonEscape(lic.licenseType) << "\"";
            j << ",\"licExpiry\":\"" << JsonEscape(lic.licExpiry) << "\"";
            j << ",\"maxActivations\":" << lic.maxActivations;
            j << ",\"currentActivations\":" << lic.currentActivations;
            j << ",\"message\":\"" << JsonEscape(lic.message) << "\"";
            j << "}";
        } else {
            j << ",\"license\":null";
        }
    }

    // ── Status ──
    j << ",\"status\":\"\"";

    j << "}";
    return j.str();
}

// ─── Command dispatch ────────────────────────────────────────────────────────
// Commands from JS are JSON: {"cmd":"selectBank","args":{"bank":3}}
// We parse with simple string matching (no JSON library dependency).

static std::string GetJsonField(const std::string& json, const char* field) {
    std::string needle = std::string("\"") + field + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.length();
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.length()) return "";
    if (json[pos] == '"') {
        // String value
        pos++;
        size_t end = pos;
        while (end < json.length() && json[end] != '"') {
            if (json[end] == '\\') end++;
            end++;
        }
        return json.substr(pos, end - pos);
    } else {
        // Number or boolean
        size_t end = pos;
        while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') end++;
        return json.substr(pos, end - pos);
    }
}

static int GetJsonInt(const std::string& json, const char* field, int defVal = 0) {
    std::string val = GetJsonField(json, field);
    if (val.empty()) return defVal;
    return atoi(val.c_str());
}

static float GetJsonFloat(const std::string& json, const char* field, float defVal = 0.f) {
    std::string val = GetJsonField(json, field);
    if (val.empty()) return defVal;
    return (float)atof(val.c_str());
}

static bool GetJsonBool(const std::string& json, const char* field, bool defVal = false) {
    std::string val = GetJsonField(json, field);
    if (val == "true") return true;
    if (val == "false") return false;
    return defVal;
}

static std::string GetJsonString(const std::string& json, const char* field) {
    return GetJsonField(json, field);
}


void DispatchCommand(TmUIData* d, const char* json) {
    if (!d || !json) return;
    std::string j(json);

    std::string cmd = GetJsonString(j, "cmd");
    if (cmd.empty()) return;

    TM_INFO("Dispatch: %s", cmd.c_str());

    // ── Tab commands ──
    if (cmd == "selectTab") {
        d->curTab = GetJsonInt(j, "tab", 0);
    }
    else if (cmd == "closeWindow") {
        // Platform-specific: handled by the webview host
    }
    else if (cmd == "init") {
        // JS requesting initial state — the host will push it
    }

    // ── Bank commands ──
    else if (cmd == "selectBank") {
        d->grid.curBank = GetJsonInt(j, "bank", 0);
        d->grid.selectedSlot = -1;
    }

    // ── Slot commands ──
    else if (cmd == "clickSlot") {
        d->grid.selectedSlot = GetJsonInt(j, "slot", -1);
    }
    else if (cmd == "activateSlot") {
        int slot = GetJsonInt(j, "slot", -1);
        int bank = d->grid.curBank;
        if (bank >= 0 && bank < NUM_BANKS && slot >= 0 && slot < SLOTS_PER_BANK) {
            SlotData& sd = d->grid.banks[bank][slot];
            if (sd.hasFile && d->host) {
                d->host->HostPlayMedia(sd.filePath, sd.scaleMode);
                d->sidebar.playing = true;
            }
        }
    }
    else if (cmd == "openFile") {
        // Platform-specific: show file dialog, then set slot path
        // The host will handle this
    }
    else if (cmd == "clearSlot") {
        int bank = GetJsonInt(j, "bank", d->grid.curBank);
        int slot = GetJsonInt(j, "slot", -1);
        if (bank >= 0 && bank < NUM_BANKS && slot >= 0 && slot < SLOTS_PER_BANK) {
            SlotData& sd = d->grid.banks[bank][slot];
            sd.filePath[0] = L'\0';
            sd.displayName[0] = L'\0';
            sd.hasFile = false;
        }
    }
    else if (cmd == "moveSlot") {
        int srcBank = GetJsonInt(j, "srcBank", -1);
        int srcSlot = GetJsonInt(j, "srcSlot", -1);
        int dstBank = GetJsonInt(j, "dstBank", -1);
        int dstSlot = GetJsonInt(j, "dstSlot", -1);
        if (srcBank >= 0 && srcSlot >= 0 && dstBank >= 0 && dstSlot >= 0 &&
            srcBank < NUM_BANKS && dstBank < NUM_BANKS &&
            srcSlot < SLOTS_PER_BANK && dstSlot < SLOTS_PER_BANK) {
            SlotData& src = d->grid.banks[srcBank][srcSlot];
            SlotData& dst = d->grid.banks[dstBank][dstSlot];
            // Swap
            SlotData tmp = src;
            src = dst;
            dst = tmp;
        }
        d->slotMovePending = false;
    }
    else if (cmd == "armMove") {
        d->slotMoveSourceBank = GetJsonInt(j, "bank", d->grid.curBank);
        d->slotMoveSource = GetJsonInt(j, "slot", -1);
        d->slotMovePending = true;
    }
    else if (cmd == "cancelMove") {
        d->slotMovePending = false;
    }

    // ── Slideshow commands ──
    else if (cmd == "slideshowToggle") {
        d->sidebar.ssEnabled = !d->sidebar.ssEnabled;
        if (d->sidebar.ssEnabled && !d->sidebar.ssRunning) {
            if (d->host) d->host->HostSlideshowStart();
            d->sidebar.ssRunning = true;
        } else if (!d->sidebar.ssEnabled && d->sidebar.ssRunning) {
            if (d->host) d->host->HostSlideshowStop();
            d->sidebar.ssRunning = false;
        }
    }
    else if (cmd == "delayInc") {
        if (d->sidebar.ssDelaySec < 60) d->sidebar.ssDelaySec++;
    }
    else if (cmd == "delayDec") {
        if (d->sidebar.ssDelaySec > 1) d->sidebar.ssDelaySec--;
    }
    else if (cmd == "transitionCycle") {
        d->sidebar.ssTransition = (d->sidebar.ssTransition + 1) % 3;
        if (d->host) d->host->HostTransitionMode(d->sidebar.ssTransition);
    }
    else if (cmd == "directionCycle") {
        d->sidebar.ssDirection = (d->sidebar.ssDirection + 1) % 3;
    }
    else if (cmd == "loopToggle") {
        d->sidebar.ssLoop = !d->sidebar.ssLoop;
    }

    // ── Scale mode ──
    else if (cmd == "scaleAspect") { d->sidebar.scaleMode = SCALE_ASPECT; if (d->host) d->host->HostScaleMode(SCALE_ASPECT); }
    else if (cmd == "scaleStretch") { d->sidebar.scaleMode = SCALE_STRETCH; if (d->host) d->host->HostScaleMode(SCALE_STRETCH); }
    else if (cmd == "scaleNoscale") { d->sidebar.scaleMode = SCALE_NOSCALE; if (d->host) d->host->HostScaleMode(SCALE_NOSCALE); }

    // ── Render mode ──
    else if (cmd == "renderAuto") { d->sidebar.renderMode = TM_RENDER_AUTO; if (d->host) d->host->HostRenderMode(TM_RENDER_AUTO); }
    else if (cmd == "renderCpu") { d->sidebar.renderMode = TM_RENDER_CPU; if (d->host) d->host->HostRenderMode(TM_RENDER_CPU); }
    else if (cmd == "renderGpu") { d->sidebar.renderMode = TM_RENDER_GPU; if (d->host) d->host->HostRenderMode(TM_RENDER_GPU); }

    // ── Layout panel commands ──
    else if (cmd == "addMain") { /* Add main panel */ }
    else if (cmd == "addImage") { /* Add image panel */ }
    else if (cmd == "addText") { /* Add text panel */ }
    else if (cmd == "addShader") { /* Add shader panel */ }
    else if (cmd == "addSong") { /* Add song panel */ }
    else if (cmd == "deletePanel") {
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
            for (int i = d->selectedPanel; i < d->numLayoutPanels - 1; i++) {
                d->layoutPanels[i] = d->layoutPanels[i + 1];
            }
            d->numLayoutPanels--;
            d->selectedPanel = -1;
        }
    }
    else if (cmd == "selectPanel") {
        d->selectedPanel = GetJsonInt(j, "index", -1);
    }
    else if (cmd == "movePanel") {
        int idx = GetJsonInt(j, "index", -1);
        if (idx >= 0 && idx < d->numLayoutPanels) {
            d->layoutPanels[idx].x = GetJsonFloat(j, "x", d->layoutPanels[idx].x);
            d->layoutPanels[idx].y = GetJsonFloat(j, "y", d->layoutPanels[idx].y);
        }
    }
    else if (cmd == "resizePanel") {
        int idx = GetJsonInt(j, "index", -1);
        if (idx >= 0 && idx < d->numLayoutPanels) {
            d->layoutPanels[idx].w = GetJsonFloat(j, "w", d->layoutPanels[idx].w);
            d->layoutPanels[idx].h = GetJsonFloat(j, "h", d->layoutPanels[idx].h);
        }
    }
    else if (cmd == "setPanelProperty") {
        int idx = GetJsonInt(j, "index", -1);
        std::string prop = GetJsonString(j, "prop");
        if (idx >= 0 && idx < d->numLayoutPanels) {
            OverlayPanel& p = d->layoutPanels[idx];
            if (prop == "x") p.x = GetJsonFloat(j, "value");
            else if (prop == "y") p.y = GetJsonFloat(j, "value");
            else if (prop == "w") p.w = GetJsonFloat(j, "value");
            else if (prop == "h") p.h = GetJsonFloat(j, "value");
            else if (prop == "shaderIndex") p.shaderIndex = GetJsonInt(j, "value");
            // textContent is a string — need wider parsing
        }
    }
    else if (cmd == "zUp") {
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
            d->layoutPanels[d->selectedPanel].zOrder++;
        }
    }
    else if (cmd == "zDown") {
        if (d->selectedPanel >= 0 && d->selectedPanel < d->numLayoutPanels) {
            d->layoutPanels[d->selectedPanel].zOrder--;
        }
    }
    else if (cmd == "directSelect") {
        d->directSelectEnabled = !d->directSelectEnabled;
    }
    else if (cmd == "loadPanelImage") {
        // Platform-specific: show file dialog
    }

    // ── Options commands ──
    else if (cmd == "optScaleAspect") { d->sidebar.scaleMode = SCALE_ASPECT; if (d->host) d->host->HostScaleMode(SCALE_ASPECT); }
    else if (cmd == "optScaleStretch") { d->sidebar.scaleMode = SCALE_STRETCH; if (d->host) d->host->HostScaleMode(SCALE_STRETCH); }
    else if (cmd == "optScaleNoscale") { d->sidebar.scaleMode = SCALE_NOSCALE; if (d->host) d->host->HostScaleMode(SCALE_NOSCALE); }
    else if (cmd == "optTransCut") { d->sidebar.ssTransition = 0; if (d->host) d->host->HostTransitionMode(0); }
    else if (cmd == "optTransFade") { d->sidebar.ssTransition = 1; if (d->host) d->host->HostTransitionMode(1); }
    else if (cmd == "optTransCross") { d->sidebar.ssTransition = 2; if (d->host) d->host->HostTransitionMode(2); }
    else if (cmd == "optDirFwd") { d->sidebar.ssDirection = 0; }
    else if (cmd == "optDirBack") { d->sidebar.ssDirection = 1; }
    else if (cmd == "optDirRandom") { d->sidebar.ssDirection = 2; }
    else if (cmd == "optLoopToggle") { d->sidebar.ssLoop = !d->sidebar.ssLoop; }
    else if (cmd == "optDelayInc") { if (d->sidebar.ssDelaySec < 60) d->sidebar.ssDelaySec++; }
    else if (cmd == "optDelayDec") { if (d->sidebar.ssDelaySec > 1) d->sidebar.ssDelaySec--; }
    else if (cmd == "optShaderEnable") {
        d->sidebar.shadersEnabled = !d->sidebar.shadersEnabled;
        if (d->host) d->host->HostSetShadersEnabled(d->sidebar.shadersEnabled);
    }
    else if (cmd == "optShaderKaraoke") {
        d->sidebar.shaderDisableOnKaraoke = !d->sidebar.shaderDisableOnKaraoke;
        if (d->host) d->host->HostSetShaderKaraokeDisable(d->sidebar.shaderDisableOnKaraoke);
    }
    else if (cmd == "optShaderPrev") {
        if (d->sidebar.mainShaderIndex > 0) d->sidebar.mainShaderIndex--;
        if (d->host) d->host->HostSetMainShader(d->sidebar.mainShaderIndex);
    }
    else if (cmd == "optShaderNext") {
        d->sidebar.mainShaderIndex++;
        if (d->host) d->host->HostSetMainShader(d->sidebar.mainShaderIndex);
    }
    else if (cmd == "optShaderReload") {
        if (d->host) d->host->HostReloadShaders();
    }
    else if (cmd == "optKaraokeAutohide") {
        d->karaokeAutoHide = !d->karaokeAutoHide;
        if (d->host) d->host->HostSetKaraokeAutoHide(d->karaokeAutoHide);
    }

    // ── License commands ──
    else if (cmd == "licenseLogin") {
        std::string email = GetJsonString(j, "email");
        std::string password = GetJsonString(j, "password");
        bool save = GetJsonBool(j, "save", false);
        if (d->host) {
            // Convert to wchar_t
            wchar_t wEmail[256] = {0};
            wchar_t wPass[256] = {0};
            mbstowcs(wEmail, email.c_str(), 255);
            mbstowcs(wPass, password.c_str(), 255);
            d->host->HostLicenseLogin(wEmail, wPass, save);
        }
    }
    else if (cmd == "licenseValidate") {
        if (d->host) d->host->HostLicenseValidate();
    }
    else if (cmd == "licenseActivate") {
        if (d->host) d->host->HostLicenseActivate();
    }
    else if (cmd == "licenseLogout") {
        if (d->host) d->host->HostLicenseLogout();
    }

    // ── Save state after any command ──
    TmUI::SaveState(d);
}

} // namespace TmWebView
