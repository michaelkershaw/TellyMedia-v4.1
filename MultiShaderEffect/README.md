MultiShaderEffect DLL (scaffold)

This project bundles your default shaders into ONE plugin DLL (no extra resource DLLs) and can also optionally discover new shaders dropped into your shaders folder.

What you get
- Embedded defaults (precompiled .cso inside the DLL) for instant, hitch‑free loads.
- Optional folder scan to lazy‑load additional .cso files, or (optionally) compile .hlsl once and cache the .cso.
- Only the selected shader is created on demand; safe fallback frame while loading.

This repo contains two utility scripts to prepare the embedded bytecode header:
- tools/build_csos.bat: Precompiles your .hlsl into .cso using fxc (SM5.0)
- tools/embed_csos.ps1: Converts the .cso files into a single C++ header (src/EmbeddedShaders.h)

Prerequisites
- Windows 10/11 with Windows SDK (fxc.exe).
- Visual Studio 2022 (recommended) or 2019.
- VirtualDJ C++ SDK (for the actual plugin project that will include src/EmbeddedShaders.h). Set an environment var VDJ_SDK_DIR to its root when we wire the plugin.

Default shader list (embedded)
- SpeakerConeDistortion.hlsl
- FloatingHolographicRings.hlsl
- NorthernLightsAurora.hlsl
- NorthernLightsAuroraLite.hlsl
- ExplodingParticleGalaxy.hlsl
- ExplodingParticleGalaxyLite.hlsl
- HypnoticSpiral.hlsl

Paths
- Source HLSL folder (read): C:\Users\michael\AppData\Local\VirtualDJ\Plugins64\VideoEffect\shaders
- Output CSO folder (write): MultiShaderEffect\csos
- Generated header: MultiShaderEffect\src\EmbeddedShaders.h

How to generate the embedded header
1) Open a Developer PowerShell for VS.
2) Run: tools\build_csos.bat
   - This finds fxc, compiles each HLSL to .cso into .\csos
   - Then calls tools\embed_csos.ps1 to generate src\EmbeddedShaders.h
3) Include src\EmbeddedShaders.h in your plugin project and link it.

Notes
- If a shader is missing or fails to compile, the script will report an error and skip it; the header will still be generated with the others.
- We’ll wire a parameter in the plugin to switch between these embedded shaders by name; the plugin will bind the precompiled bytecode directly (no runtime D3DCompile).
- If you also want runtime discovery: the plugin can scan your shaders folder and prefer .cso files there. We’ll add that in code once you confirm the SDK path.

Next step
- Share (1) your Visual Studio version and (2) where your VirtualDJ SDK is located. I’ll add the plugin project that consumes EmbeddedShaders.h and exposes a single VideoEffect with a dropdown to select the shader.
