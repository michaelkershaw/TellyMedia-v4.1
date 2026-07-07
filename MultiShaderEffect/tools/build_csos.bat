@echo off
setlocal enabledelayedexpansion

rem Builds .cso bytecode for your shaders using fxc (Shader Model 5.0) and then generates EmbeddedShaders.h

rem Locate fxc (prefer VS/Windows SDK path if available)
set FXC_EXE=
for %%i in ("%WindowsSdkDir%bin\x64\fxc.exe","%ProgramFiles(x86)%\Windows Kits\10\bin\x64\fxc.exe","%ProgramFiles(x86)%\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe","%ProgramFiles(x86)%\Windows Kits\10\bin\10.0.19041.0\x64\fxc.exe") do (
  if exist %%~i set FXC_EXE=%%~i
)
if "%FXC_EXE%"=="" (
  where fxc >nul 2>&1 && set FXC_EXE=fxc
)
if "%FXC_EXE%"=="" (
  echo ERROR: Could not find fxc.exe. Install Windows 10/11 SDK and ensure fxc is on PATH.
  exit /b 1
)

echo Using fxc: %FXC_EXE%

set SRC_DIR=C:\Users\michael\AppData\Local\VirtualDJ\Plugins64\VideoEffect\shaders
set OUT_DIR=%~dp0..\csos
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

set SHADERS=SpeakerConeDistortion.hlsl FloatingHolographicRings.hlsl NorthernLightsAurora.hlsl ExplodingParticleGalaxy.hlsl HypnoticSpiral.hlsl CircularSpectrum.hlsl AnimatedDanceFloor.hlsl MassiveSpeakerConeDistortionColored.hlsl GoldAbstractCircles.hlsl VibrantNebula.hlsl

for %%S in (%SHADERS%) do (
  if exist "%SRC_DIR%\%%S" (
    echo Compiling %%S ...
    "%FXC_EXE%" /T ps_5_0 /E PSMain /O3 /Ges /Fo "%OUT_DIR%\%%~nS.cso" "%SRC_DIR%\%%S"
    if errorlevel 1 (
      echo ERROR compiling %%S
    )
  ) else (
    echo WARNING: Not found: %SRC_DIR%\%%S
  )
)

powershell -ExecutionPolicy Bypass -File "%~dp0embed_csos.ps1" -InputDir "%OUT_DIR%" -OutputHeader "%~dp0..\src\EmbeddedShaders.h"
if errorlevel 1 (
  echo ERROR: Failed to generate EmbeddedShaders.h
  exit /b 1
)

echo Done. Generated src\EmbeddedShaders.h
