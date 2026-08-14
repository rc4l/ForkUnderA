#Requires -Version 5.1
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

<#
.SYNOPSIS
    Compile ZandroX on Windows from the in-repo source.

.DESCRIPTION
    Builds ZandroX (x64) the same way CI does: vcpkg-supplied OpenAL audio stack,
    the DirectX headers reshaped out of the modern Windows SDK, MSVC via the
    Visual Studio 2022 generator, then packages a runnable dist-windows/ zip.

    This is the "compile from source" path — it (re)builds the dependencies with
    vcpkg. For a fast build from prebuilt, committed dependencies, use
    windows_build.ps1 instead.

    De-Zandronum principle — this script is for ZandroX, not upstream Zandronum,
    and must not regress to Zandronumisms:
      * OpenAL only. FMOD is gone; never reintroduce it (NO_FMOD=ON).
      * It compiles the source already in this repo (src/zandronum). It does NOT
        download or check out Zandronum, and there is no ZA_3.2.1 default ref.
      * Output is branded ZandroX.

.PARAMETER Configuration
    Debug or Release (default: Release).

.PARAMETER Version
    Version string baked into the zip name (default: dev-<short git sha>).

.PARAMETER Clean
    Remove the build and dist directories before building.

.PARAMETER SkipDeps
    Skip the vcpkg dependency install (faster for an incremental rebuild).

.PARAMETER NoPackage
    Stop after compiling; do not assemble or zip dist-windows/.

.EXAMPLE
    .\windows_compile.ps1
    .\windows_compile.ps1 -Configuration Debug -NoPackage
    .\windows_compile.ps1 -Clean -Version v0.2.0
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$Version = "",

    [switch]$Clean,
    [switch]$SkipDeps,
    [switch]$NoPackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptRoot = $PSScriptRoot
$BuildDir   = Join-Path $ScriptRoot "build-win"
$DistDir    = Join-Path $ScriptRoot "dist-windows"
$DepsDir    = Join-Path $ScriptRoot "deps"

function Write-Status { param([string]$Message) Write-Host "==> $Message" -ForegroundColor Green }
function Write-Note   { param([string]$Message) Write-Host "    $Message" -ForegroundColor DarkGray }

function Get-DefaultVersion {
    # [rc4l] dev-<short sha> when the caller did not pass a version, matching the workflow's
    # naming for non-release builds. Falls back to plain "dev" outside a git checkout.
    try {
        $sha = (& git -C $ScriptRoot rev-parse --short=8 HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $sha) { return "dev-$sha" }
    } catch { }
    return "dev"
}

function Require-Command {
    param([string]$Name, [string]$Hint)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "$Name not found on PATH. $Hint" }
    return $cmd.Source
}

function Resolve-Vcpkg {
    # [rc4l] Prefer an existing vcpkg (CI sets VCPKG_INSTALLATION_ROOT; devs often have it on
    # PATH). Otherwise bootstrap a private copy under deps/ so a fresh machine still works.
    if ($env:VCPKG_INSTALLATION_ROOT -and (Test-Path (Join-Path $env:VCPKG_INSTALLATION_ROOT "vcpkg.exe"))) {
        Write-Note "Using vcpkg from VCPKG_INSTALLATION_ROOT: $env:VCPKG_INSTALLATION_ROOT"
        return $env:VCPKG_INSTALLATION_ROOT
    }
    $onPath = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
    if ($onPath) {
        $root = Split-Path $onPath.Source -Parent
        Write-Note "Using vcpkg from PATH: $root"
        return $root
    }
    $local = Join-Path $DepsDir "vcpkg"
    if (-not (Test-Path (Join-Path $local "vcpkg.exe"))) {
        Write-Status "Bootstrapping a local vcpkg into $local"
        Require-Command "git" "Install Git for Windows." | Out-Null
        New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null
        if (-not (Test-Path (Join-Path $local ".git"))) {
            & git clone --depth 1 https://github.com/microsoft/vcpkg.git $local
            if ($LASTEXITCODE -ne 0) { throw "git clone of vcpkg failed" }
        }
        & (Join-Path $local "bootstrap-vcpkg.bat") -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw "bootstrap-vcpkg.bat failed" }
    }
    return $local
}

# --- Clean -----------------------------------------------------------------
if ($Clean) {
    Write-Status "Cleaning build and dist directories"
    foreach ($d in @($BuildDir, $DistDir)) {
        if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    }
}

if (-not $Version) { $Version = Get-DefaultVersion }
Write-Status "ZandroX Windows compile — configuration=$Configuration version=$Version"

# --- Tooling ---------------------------------------------------------------
Require-Command "cmake" "Install CMake and Visual Studio 2022 (with the C++ workload)." | Out-Null
$VcpkgRoot      = Resolve-Vcpkg
$VcpkgExe       = Join-Path $VcpkgRoot "vcpkg.exe"
$VcpkgInstalled = Join-Path $VcpkgRoot "installed\x64-windows-static"

# --- Dependencies (OpenAL stack — never FMOD) ------------------------------
if ($SkipDeps) {
    Write-Status "Skipping vcpkg install (-SkipDeps)"
} else {
    Write-Status "Installing OpenAL audio dependencies via vcpkg (first run is slow)"
    # [rc4l] Flight 1: glew replaces the hand-rolled GL loader (gl/api) on Windows too -- one
    # loader on every platform, per upstream 69af73d9b/94b06900c.
    & $VcpkgExe install `
        openal-soft:x64-windows-static libsndfile:x64-windows-static mpg123:x64-windows-static `
        opus:x64-windows-static openssl:x64-windows-static glew:x64-windows-static `
        ffmpeg[x264]:x64-windows-static
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }
}

# --- DirectX headers/libs from the Windows SDK -----------------------------
# [rc4l] Zandronum's build wants the legacy DirectX SDK layout ($DXSDK_DIR/Include/d3d9.h,
# $DXSDK_DIR/Lib/x64/dxguid.lib). Those files live in the modern Windows SDK — reshape them.
Write-Status "Setting up DirectX headers/libs from the Windows SDK"
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
if (-not (Test-Path $kits)) { $kits = "${env:ProgramFiles}\Windows Kits\10" }
if (-not (Test-Path $kits)) { throw "Windows 10 SDK not found — install it via the Visual Studio installer." }
$ver = (Get-ChildItem "$kits\Include" -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+' } |
        Sort-Object Name -Descending | Select-Object -First 1).Name
Write-Note "Windows SDK version: $ver"
$dx = Join-Path $ScriptRoot "dxsdk"
New-Item -ItemType Directory -Force -Path "$dx\Include", "$dx\Lib\x64" | Out-Null
Copy-Item "$kits\Include\$ver\shared\*" "$dx\Include\" -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item "$kits\Include\$ver\um\*"     "$dx\Include\" -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item "$kits\Lib\$ver\um\x64\*"     "$dx\Lib\x64\" -Force -ErrorAction SilentlyContinue
if (-not (Test-Path "$dx\Include\d3d9.h"))     { throw "d3d9.h not found in Windows SDK $ver" }
if (-not (Test-Path "$dx\Lib\x64\dxguid.lib")) { throw "dxguid.lib not found in Windows SDK $ver" }
$env:DXSDK_DIR = $dx
Write-Note "DXSDK_DIR set to $dx"

# --- Configure (MSVC x64, NO_FMOD, OpenAL) ---------------------------------
# [rc4l] Explicit -D dep paths instead of the vcpkg toolchain file — the toolchain's
# cmake_policy() calls collide with Zandronum's old CMake minimums and break the VS generator.
Write-Status "Configuring CMake (Visual Studio 2022, x64, OpenAL, STATIC deps)"
# [rc4l] Kept so the replay assertion below has something to read.
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$ConfigureLog = Join-Path $BuildDir "configure.log"
$dep = $VcpkgInstalled
# [rc4l] Static deps (x64-windows-static): link the whole vcpkg static lib set -- the linker
# discards what it doesn't reference -- so libsndfile's transitive codecs (FLAC/vorbis/ogg/opus/
# mpg123/LAME) resolve without hand-listing, plus the Win32 system libs static OpenAL-soft/OpenSSL
# need beyond what the engine already links (advapi32/bcrypt/avrt). Fed via SNDFILE_LIBRARY, whose
# entries are consumed as file PATHS -- so the system libs must be full paths too (bare names get
# resolved relative to the build dir and fail). The SDK's um\x64 libs were copied into $dx\Lib\x64.
# [rc4l] avrt/bcrypt cover the audio stack; the mf*/secur32/strmiids/ws2_32 set is what static ffmpeg
# (libav*) pulls in on Windows for its Media Foundation + networking + DirectShow glue.
$sysLibs = @('advapi32','bcrypt','ncrypt','avrt','secur32','ws2_32','mfplat','mfuuid','strmiids','ole32','user32','shlwapi') |
    ForEach-Object { Join-Path "$dx\Lib\x64" ($_ + '.lib') }
$staticLibs = ((Get-ChildItem "$dep\lib\*.lib").FullName + $sysLibs) -join ';'
# [rc4l] Flight 1 needs GLEW on Windows too. vcpkg names the static lib libglew32.lib (not
# glew32.lib), which CMake's find_library(NAMES GLEW glew32) won't match by default -- resolve it by
# glob and pass GLEW_INCLUDE_DIR/GLEW_LIBRARY explicitly. (It also lands in $staticLibs via the glob.)
$glewLib = (Get-ChildItem "$dep\lib" -Filter "*glew*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1)
if (-not $glewLib) { throw "no *glew*.lib found in $dep\lib" }
Write-Note "Resolved GLEW static lib: $($glewLib.Name)"
# [rc4l] ZX_WITH_SYMBOLS=1 (release CI) emits a program PDB for symbol upload. We pass it as a
# cache var, NOT via CMAKE_CXX_FLAGS: overriding CMAKE_CXX_FLAGS wipes MSVC's default /DWIN32
# /D_WINDOWS defines and breaks the build. src/CMakeLists.txt adds /Zi + /DEBUG per-target instead.
$symArgs = @()
if ($env:ZX_WITH_SYMBOLS -eq "1") {
    Write-Status "building with debug symbols (PDB)"
    $symArgs = @("-DZX_WITH_SYMBOLS=ON")
}

# [rc4l] Only a build whose symbols get published may report crashes; see ZX_OFFICIAL_BUILD in
# src/zandronum/CMakeLists.txt. Set by CI, never by a local build.
if ($env:ZX_OFFICIAL_BUILD -eq "1") {
    Write-Status "official build: crash reporting enabled"
    $symArgs += "-DZX_OFFICIAL_BUILD=ON"
}
& cmake -S (Join-Path $ScriptRoot "src\zandronum") -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T v143 `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
    "-DCMAKE_PREFIX_PATH=$dep" `
    -DNO_FMOD=ON -DNO_OPENAL=OFF `
    -DFORCE_INTERNAL_JPEG=ON -DFORCE_INTERNAL_BZIP2=ON -DFORCE_INTERNAL_ZLIB=ON `
    -DFORCE_INTERNAL_GME=ON `
    "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /DAL_LIBTYPE_STATIC /DGLEW_STATIC" `
    "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /DAL_LIBTYPE_STATIC /DGLEW_STATIC" `
    "-DOPENAL_INCLUDE_DIR=$dep/include/AL" `
    "-DOPENAL_LIBRARY=$dep/lib/OpenAL32.lib" `
    "-DSNDFILE_INCLUDE_DIR=$dep/include" `
    "-DSNDFILE_LIBRARY=$staticLibs" `
    "-DMPG123_INCLUDE_DIR=$dep/include" `
    "-DMPG123_LIBRARIES=$dep/lib/mpg123.lib" `
    "-DOPUS_INCLUDE_DIR=$dep/include/opus" `
    "-DOPUS_LIBRARIES=$dep/lib/opus.lib" `
    "-DGLEW_INCLUDE_DIR=$dep/include" `
    "-DGLEW_LIBRARY=$($glewLib.FullName)" `
    "-DOPENSSL_ROOT_DIR=$dep" "-DOPENSSL_USE_STATIC_LIBS=ON" `
    @symArgs | Tee-Object -FilePath $ConfigureLog
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

# [rc4l] Instant replay is OPTIONAL to CMake -- without FFmpeg it compiles as a no-capture stub and
# nothing fails -- so the only way to tell a full build from a feature-stripped one is this line in
# the configure output. CI already asserts on it (_build.yml greps build.log); asserting here too
# means a local run of this script cannot quietly produce something a release never would.
if (-not (Select-String -Path $ConfigureLog -Pattern "FUA replay: FFmpeg found" -Quiet)) {
    throw "CMake did not enable instant replay (no 'FUA replay: FFmpeg found' in $ConfigureLog). Re-run without -SkipDeps so vcpkg installs ffmpeg[x264]."
}

# --- Build -----------------------------------------------------------------
Write-Status "Building ($Configuration)"
& cmake --build $BuildDir --config $Configuration -- -m
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$exe = Join-Path $BuildDir "$Configuration\forkundera.exe"
if (-not (Test-Path $exe)) { throw "forkundera.exe missing — the build failed" }
Write-Status "Compiled: $exe"

if ($NoPackage) {
    Write-Status "Done (compile only; -NoPackage)"
    return
}

# --- Package (mirrors CI so a dev build matches a release) ------------------
Write-Status "Packaging dist-windows/"
$out = Join-Path $BuildDir $Configuration
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item "$out\forkundera.exe" $DistDir\
Copy-Item "$out\*.pk3" $DistDir\ -ErrorAction SilentlyContinue

# [rc4l] Ship Freedoom so the zip is playable without a separate IWAD (BSD-3-clause, clause 2
# requires the notice to accompany binary distributions).
# Both phases: Phase 1 is what stands in for doom.wad, and the wildcard copy below would happily
# ship half of them.
foreach ($wad in @("freedoom1.wad", "freedoom2.wad")) {
    if (-not (Test-Path (Join-Path $ScriptRoot "tools\freedoom\$wad"))) {
        throw "tools/freedoom/$wad missing — the zip would ship without a game to fall back on"
    }
}
Copy-Item (Join-Path $ScriptRoot "tools\freedoom\*.wad") $DistDir\
Copy-Item (Join-Path $ScriptRoot "tools\freedoom\License.txt") "$DistDir\FREEDOOM-LICENSE.txt"

# [rc4l] The addon catalogue. Required rather than best-effort: the HOST tab reads it from beside the
# exe, and a build without it offers nothing to host.
$catalogue = Join-Path $ScriptRoot "catalogue"
if (-not (Test-Path $catalogue)) { throw "catalogue/ missing -- the HOST tab would have nothing to offer" }
Copy-Item $catalogue $DistDir -Recurse -Force

# [rc4l] GPL-3.0 sections 4-6: the binary must carry the license text and point at the source.
Copy-Item (Join-Path $ScriptRoot "LICENSE.txt") $DistDir\
Copy-Item (Join-Path $ScriptRoot "THIRD-PARTY-NOTICES.txt") $DistDir\

# [rc4l] Deps are statically linked (x64-windows-static), so there are NO runtime DLLs to ship;
# the app folder is just the exe + pk3s. Guard against a silent regression to dynamic linking.
if (Get-ChildItem "$DistDir\*.dll" -ErrorAction SilentlyContinue) {
    throw "unexpected DLL(s) in dist-windows — deps should be static; check the vcpkg triplet"
}
Write-Note "static build: no runtime DLLs in dist-windows"

$zip = Join-Path $ScriptRoot "ForkUnderA-$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$DistDir\*" -DestinationPath $zip -Force
Write-Status "Done: $zip"
