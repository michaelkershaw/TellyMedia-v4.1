cmake_minimum_required(VERSION 3.20)
project(TellyMediaV4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /EHsc /utf-8)
    add_compile_definitions(UNICODE _UNICODE)
endif()

# ─── Platform detection ──────────────────────────────────────────────────────
if(APPLE)
    set(TM_PLATFORM mac)
    set(TM_PLATFORM_DIR mac)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
elseif(WIN32)
    set(TM_PLATFORM win)
    set(TM_PLATFORM_DIR win)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

set(TM_INCLUDE ${CMAKE_SOURCE_DIR}/include)

# ─── Shared sources (platform-agnostic) ──────────────────────────────────────
set(TM_SHARED_SOURCES
    src/ui/TmWebView.cpp
)

if(WIN32)
    # Windows: keep existing GDI+ UI + WinHTTP services
    list(APPEND TM_SHARED_SOURCES
        src/services/TmLogger.cpp
        src/services/TmServices.cpp
        src/ui/TmTheme.cpp
        src/ui/TmUI.cpp
    )
elseif(APPLE)
    # macOS: WKWebView UI + URLSession/Keychain services
    list(APPEND TM_SHARED_SOURCES
        src/services/TmLoggerMac.mm
        src/services/TmServicesMac.mm
        src/ui/TmWebView.mm
        src/ui/TmUIMac.mm
    )
endif()

# ─── macOS: VirtualDJ plugin .bundle ─────────────────────────────────────────
if(APPLE)
    # Build as .bundle (loadable by VirtualDJ)
    add_library(TellyMediaReborn MODULE
        ${TM_SHARED_SOURCES}
        src/render/TmRendererMac.mm
        src/media/TmMediaMac.mm
        src/core/TmPluginMac.mm
        src/core/mac_main.mm
    )
    target_include_directories(TellyMediaReborn PRIVATE ${TM_INCLUDE})
    target_link_libraries(TellyMediaReborn PRIVATE
        "-framework Cocoa"
        "-framework WebKit"
        "-framework OpenGL"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Security"
        "-framework Foundation"
    )
    set_target_properties(TellyMediaReborn PROPERTIES
        OUTPUT_NAME "TellyMedia-reborn"
        PREFIX ""
        SUFFIX ".bundle"
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
    )

    # Copy web assets next to the bundle
    add_custom_command(TARGET TellyMediaReborn POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/web"
            "$<TARGET_FILE_DIR:TellyMediaReborn>/web"
        COMMENT "Copying web assets to bundle output directory"
    )

    # Copy GLSL shaders next to the bundle
    add_custom_command(TARGET TellyMediaReborn POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:TellyMediaReborn>/shaders"
        COMMENT "Copying GLSL shaders to bundle output directory"
    )

    # Deploy to VirtualDJ macOS ARM plugins folder
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$ENV{HOME}/Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:TellyMediaReborn>"
            "$ENV{HOME}/Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect/TellyMedia-reborn.bundle"
        DEPENDS TellyMediaReborn
        COMMENT "Deploying TellyMedia-reborn.bundle to VirtualDJ macOS ARM VideoEffect folder"
    )

    # Install target (for make install / cpack)
    install(TARGETS TellyMediaReborn
        LIBRARY DESTINATION Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect
        BUNDLE DESTINATION Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect
    )
    install(DIRECTORY web DESTINATION Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect/TellyMedia-reborn.bundle)
    install(DIRECTORY shaders DESTINATION Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect/TellyMedia-reborn.bundle)

    # ─── macOS standalone test app ────────────────────────────────────────────
    add_executable(TellyMediaV4_Standalone
        ${TM_SHARED_SOURCES}
        src/media/TmMediaMac.mm
        src/app/mac_standalone.mm
    )
    target_include_directories(TellyMediaV4_Standalone PRIVATE ${TM_INCLUDE})
    target_link_libraries(TellyMediaV4_Standalone PRIVATE
        "-framework Cocoa"
        "-framework WebKit"
        "-framework OpenGL"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Security"
        "-framework Foundation"
    )
    set_target_properties(TellyMediaV4_Standalone PROPERTIES
        OUTPUT_NAME "TellyMediaV4"
    )
    add_custom_command(TARGET TellyMediaV4_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/web"
            "$<TARGET_FILE_DIR:TellyMediaV4_Standalone>/web"
        COMMENT "Copying web assets to standalone app directory"
    )

endif()

# ─── Windows: existing build (unchanged) ─────────────────────────────────────
if(WIN32)
    # Phase 1: Standalone UI test harness
    add_executable(TellyMediaV4_Standalone WIN32
        ${TM_SHARED_SOURCES}
        src/media/TmMedia.cpp
        src/app/TmStandalone.cpp
    )
    target_include_directories(TellyMediaV4_Standalone PRIVATE ${TM_INCLUDE})
    target_link_libraries(TellyMediaV4_Standalone PRIVATE
        gdiplus comdlg32 shell32 shlwapi user32 gdi32 ole32 msimg32
    )

    # VirtualDJ plugin DLL
    add_library(TellyMediaReborn SHARED
        ${TM_SHARED_SOURCES}
        src/render/TmRenderer.cpp
        src/media/TmMedia.cpp
        src/core/TmPlugin.cpp
        src/core/dllmain.cpp
        tm_resources.rc
    )
    target_include_directories(TellyMediaReborn PRIVATE ${TM_INCLUDE} ${CMAKE_SOURCE_DIR}/MultiShaderEffect/src)
    target_link_libraries(TellyMediaReborn PRIVATE
        gdiplus comdlg32 shell32 shlwapi user32 gdi32 ole32 msimg32
        d3d11 d3dcompiler mfplat mfreadwrite mfuuid
    )
    set_target_properties(TellyMediaReborn PROPERTIES
        OUTPUT_NAME "TellyMedia-reborn"
        PREFIX ""
        LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/TellyMedia.def\""
    )

    # Generate EmbeddedShaders.h
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/MultiShaderEffect/src/EmbeddedShaders.h
        COMMAND cmd /c "${CMAKE_SOURCE_DIR}/MultiShaderEffect/tools/build_csos.bat"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/MultiShaderEffect/tools
    )
    add_custom_target(EmbedShadersHeader DEPENDS ${CMAKE_SOURCE_DIR}/MultiShaderEffect/src/EmbeddedShaders.h)
    add_dependencies(TellyMediaReborn EmbedShadersHeader)

    # Deploy to VirtualDJ
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$ENV{LOCALAPPDATA}/VirtualDJ/Plugins64/VideoEffect"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:TellyMediaReborn>"
            "$ENV{LOCALAPPDATA}/VirtualDJ/Plugins64/VideoEffect/TellyMedia-reborn.dll"
        DEPENDS TellyMediaReborn
        COMMENT "Deploying TellyMedia-reborn.dll to VirtualDJ VideoEffect folder"
    )
endif()
