# TellyMedia v4 — macOS Port

A VirtualDJ Video FX plugin for overlay media playback, now ported to macOS.

## Features

- **Media Playback**: Load images and videos for overlay on the master deck
- **Slideshow Mode**: Automatic cycling through media banks with transitions
- **Custom Shaders**: GLSL-based visual effects with audio reactivity
- **Layout Editor**: Position and configure overlay panels for text/images
- **Licensing**: Secure cloud-based licensing via URLSession + Keychain

## macOS Implementation

This is a complete port of the Windows version, replacing:
- **Win32/GDI+ UI** → WKWebView with HTML/CSS/JS frontend
- **Direct3D 11** → OpenGL (CGLContextObj)
- **Media Foundation** → AVFoundation
- **WinHTTP** → URLSession
- **Credential Manager** → Keychain
- **Registry** → NSUserDefaults

## Building

### Prerequisites
- macOS 10.15+ (Catalina or later)
- Xcode 12.0 or later
- CMake 3.15+
- VirtualDJ (for testing the plugin)

### Build Instructions

```bash
# Clone or navigate to the project
cd TellyMedia-v4.1

# Create build directory
mkdir build && cd build

# Generate Xcode project
cmake .. -GXcode

# Open in Xcode
open TellyMediaReborn.xcodeproj
```

Build the `TellyMediaReborn` target in Xcode. The output `.bundle` will be in `build/Release/`.

### Deploy to VirtualDJ

```bash
cd build
cmake --build . --target deploy
```

This copies the bundle to `~/Documents/VirtualDJ/Plugins/VideoEffect/TellyMedia-reborn.bundle/`.

### Standalone Test App

```bash
cd build
cmake --build . --target TellyMediaV4_Standalone
./Release/TellyMediaV4
```

## Project Structure

```
TellyMedia-v4.1/
├── include/tm/          # Platform-agnostic headers
│   ├── TmPlatform.h     # Type abstractions (HWND→NSView*, etc.)
│   ├── TmTypes.h        # Core data structures
│   ├── TmPlugin.h       # Plugin interface
│   ├── TmRenderer.h     # Rendering abstraction
│   ├── TmMedia.h        # Media engine abstraction
│   ├── TmServices.h     # HTTP/JSON/Licensing
│   ├── TmUI.h           # UI state management
│   └── TmWebView.h      # WKWebView bridge (macOS)
├── src/
│   ├── core/
│   │   ├── TmPlugin.cpp         # Windows plugin
│   │   ├── TmPluginMac.mm       # macOS plugin
│   │   ├── dllmain.cpp          # Windows DLL entry
│   │   └── mac_main.mm         # macOS bundle entry
│   ├── render/
│   │   ├── TmRenderer.cpp      # Windows D3D11
│   │   └── TmRendererMac.mm    # macOS OpenGL
│   ├── media/
│   │   ├── TmMedia.cpp         # Windows Media Foundation
│   │   └── TmMediaMac.mm       # macOS AVFoundation
│   ├── services/
│   │   ├── TmServices.cpp      # Windows WinHTTP/Registry
│   │   ├── TmServicesMac.mm    # macOS URLSession/Keychain
│   │   ├── TmLogger.cpp        # Windows logger
│   │   └── TmLoggerMac.mm      # macOS logger
│   └── ui/
│       ├── TmUI.cpp            # Windows GDI+ UI
│       ├── TmTheme.cpp         # Windows theme
│       ├── TmWebView.mm        # macOS WKWebView bridge
│       ├── TmWebView.cpp       # Shared state serialization
│       └── TmUIMac.mm          # macOS state persistence
├── web/                     # HTML/CSS/JS frontend
│   ├── index.html
│   ├── css/styles.css
│   └── js/app.js
├── shaders/                # GLSL shaders
│   ├── GoldAbstractCircles.glsl
│   ├── HypnoticSpiral.glsl
│   ├── SpeakerConeDistortion.glsl
│   ├── VibrantNebula.glsl
│   ├── NorthernLightsAurora.glsl
│   ├── FloatingHolographicRings.glsl
│   └── ExplodingParticleGalaxy.glsl
└── CMakeLists.txt          # Cross-platform build
```

## Licensing

This plugin requires a valid license key. Licenses are stored securely in the macOS Keychain.

## License Server

- URL: `https://djeventsuite.cloud/pages/api/login`
- Credentials stored in Keychain under service `com.tellymedia.reborn.authToken`

## Acknowledgments

- Original Windows implementation by DJ Micky K
- macOS port using VirtualDJ SDK for OpenGL
