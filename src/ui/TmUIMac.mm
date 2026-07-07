// TmUIMac.mm — macOS implementation of TmUI state persistence
// Provides SaveState/LoadState using ~/Library/Application Support/VirtualDJ/TellyMedia-v4/
// Replaces the Windows TmUI.cpp state functions (which use SHGetFolderPath + CreateFileW).

#if defined(VDJ_MAC)

#import <Foundation/Foundation.h>
#include "tm/TmUI.h"
#include "tm/TmLogger.h"
#include <cstring>
#include <cstdio>

namespace TmUI {

static NSString* StateDirPath() {
    NSString* base = [NSHomeDirectory() stringByAppendingPathComponent:
        @"Library/Application Support/VirtualDJ/TellyMedia-v4"];
    NSFileManager* fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:base]) {
        [fm createDirectoryAtPath:base withIntermediateDirectories:YES attributes:nil error:nil];
    }
    return base;
}

static NSString* StateFilePath() {
    return [StateDirPath() stringByAppendingPathComponent:@"state.dat"];
}

void SaveState(TmUIData* d) {
    if (!d) return;

    NSString* path = StateFilePath();

    // Build binary blob in memory (same format as Windows version for cross-platform compat)
    NSMutableData* data = [NSMutableData data];

    uint32_t magic = 0x544D5634; // 'TMV4'
    [data appendBytes:&magic length:sizeof(magic)];
    [data appendBytes:&d->grid.curBank length:sizeof(int)];
    [data appendBytes:&d->sidebar length:sizeof(SidebarSettings)];
    [data appendBytes:d->grid.bankNames length:sizeof(d->grid.bankNames)];
    [data appendBytes:&d->numLayoutPanels length:sizeof(int)];

    int panelCount = d->numLayoutPanels;
    if (panelCount < 0) panelCount = 0;
    if (panelCount > MAX_OVERLAY_PANELS) panelCount = MAX_OVERLAY_PANELS;
    for (int i = 0; i < panelCount; i++) {
        [data appendBytes:&d->layoutPanels[i] length:sizeof(OverlayPanel)];
    }

    for (int b = 0; b < NUM_BANKS; ++b) {
        for (int i = 0; i < SLOTS_PER_BANK; ++i) {
            SlotData& s = d->grid.banks[b][i];
            uint8_t has = s.hasFile ? 1 : 0;
            [data appendBytes:&has length:1];
            if (has) {
                [data appendBytes:s.filePath length:sizeof(s.filePath)];
                [data appendBytes:&s.scaleMode length:sizeof(int)];
                [data appendBytes:&s.rotation length:sizeof(int)];
                [data appendBytes:&s.builtinFx length:sizeof(int)];
            }
        }
    }

    [data writeToFile:path atomically:YES];
    TM_INFO("State saved to %s", [path UTF8String]);
}

void LoadState(TmUIData* d) {
    if (!d) return;

    NSString* path = StateFilePath();
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data || [data length] < 4) return;

    const BYTE* bytes = (const BYTE*)[data bytes];
    NSUInteger offset = 0;

    uint32_t magic = 0;
    memcpy(&magic, bytes + offset, sizeof(magic)); offset += sizeof(magic);
    if (magic != 0x544D5634) { TM_WARN("State file: bad magic"); return; }

    memcpy(&d->grid.curBank, bytes + offset, sizeof(int)); offset += sizeof(int);
    memcpy(&d->sidebar, bytes + offset, sizeof(SidebarSettings)); offset += sizeof(SidebarSettings);
    memcpy(d->grid.bankNames, bytes + offset, sizeof(d->grid.bankNames)); offset += sizeof(d->grid.bankNames);

    // Sanitize sidebar settings
    if (d->sidebar.scaleMode < 0 || d->sidebar.scaleMode > 2) d->sidebar.scaleMode = SCALE_ASPECT;
    if (d->sidebar.ssTransition < 0 || d->sidebar.ssTransition > 2) d->sidebar.ssTransition = 1;
    if (d->sidebar.ssDirection < 0 || d->sidebar.ssDirection > 2) d->sidebar.ssDirection = 0;
    if (d->sidebar.ssDelaySec < 1 || d->sidebar.ssDelaySec > 60) d->sidebar.ssDelaySec = 5;
    if (d->sidebar.renderMode < TM_RENDER_AUTO || d->sidebar.renderMode > TM_RENDER_GPU) d->sidebar.renderMode = TM_RENDER_AUTO;
    if (d->sidebar.mainShaderIndex < 0) d->sidebar.mainShaderIndex = 0;

    d->numLayoutPanels = 0;
    int savedPanels = 0;
    if (offset + sizeof(int) <= [data length]) {
        memcpy(&savedPanels, bytes + offset, sizeof(int)); offset += sizeof(int);
        if (savedPanels < 0) savedPanels = 0;
        if (savedPanels > MAX_OVERLAY_PANELS) savedPanels = MAX_OVERLAY_PANELS;
        for (int i = 0; i < savedPanels; ++i) {
            if (offset + sizeof(OverlayPanel) > [data length]) { savedPanels = i; break; }
            OverlayPanel panel;
            memcpy(&panel, bytes + offset, sizeof(OverlayPanel)); offset += sizeof(OverlayPanel);
            d->layoutPanels[i] = panel;
            d->layoutPanels[i].visible = panel.visible;
            d->layoutPanels[i].hasImage = panel.hasImage && panel.imagePath[0] != L'\0';
            d->layoutPanels[i].zOrder = panel.zOrder;
        }
        d->numLayoutPanels = savedPanels;
        if (d->numLayoutPanels > 0) d->selectedPanel = 0;
    }

    for (int b = 0; b < NUM_BANKS; ++b) {
        for (int i = 0; i < SLOTS_PER_BANK; ++i) {
            if (offset + 1 > [data length]) { TM_INFO("State loaded (partial)"); return; }
            uint8_t has = 0;
            memcpy(&has, bytes + offset, 1); offset += 1;
            if (has) {
                SlotData& s = d->grid.banks[b][i];
                if (offset + sizeof(s.filePath) > [data length]) return;
                memcpy(s.filePath, bytes + offset, sizeof(s.filePath)); offset += sizeof(s.filePath);
                if (offset + sizeof(int) > [data length]) return;
                memcpy(&s.scaleMode, bytes + offset, sizeof(int)); offset += sizeof(int);
                if (offset + sizeof(int) > [data length]) return;
                memcpy(&s.rotation, bytes + offset, sizeof(int)); offset += sizeof(int);
                if (offset + sizeof(int) > [data length]) return;
                memcpy(&s.builtinFx, bytes + offset, sizeof(int)); offset += sizeof(int);
                s.hasFile = true;
            }
        }
    }

    TM_INFO("State loaded from %s", [path UTF8String]);
}

} // namespace TmUI

#endif // VDJ_MAC
