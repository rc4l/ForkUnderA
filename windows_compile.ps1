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
# [rc4l] Fully-static triplet so the audio/GL stack links INTO the exe and the package ships (near)
# zero loose DLLs -- matching upstream's static vcpkg build. GPL app + full source makes
# static-linking the LGPL codecs compliant. Use x64-windows-static (NOT -static-md): Zandronum
# builds the engine with the static CRT (/MT) to avoid a VC-redist dependency, so the vcpkg libs
# must be /MT too, or the linker rejects the MD/MT RuntimeLibrary mismatch.
$VcpkgTriplet   = "x64-windows-static"
$VcpkgInstalled = Join-Path $VcpkgRoot "installed\$VcpkgTriplet"

# --- Dependencies (OpenAL stack — never FMOD) ------------------------------
if ($SkipDeps) {
    Write-Status "Skipping vcpkg install (-SkipDeps)"
} else {
    Write-Status "Installing OpenAL audio dependencies via vcpkg (first run is slow)"
    # [rc4l] Flight 1: glew replaces the hand-rolled GL loader (gl/api) on Windows too -- one
    # loader on every platform, per upstream 69af73d9b/94b06900c.
    & $VcpkgExe install `
        "openal-soft:$VcpkgTriplet" "libsndfile:$VcpkgTriplet" "mpg123:$VcpkgTriplet" `
        "opus:$VcpkgTriplet" "openssl:$VcpkgTriplet" "glew:$VcpkgTriplet"
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
$dep = $VcpkgInstalled
# [rc4l] Static linking. We don't use the vcpkg toolchain + find_package(CONFIG) (which would pull
# transitive static deps automatically) because the toolchain's cmake_policy() calls break the VS
# generator against Zandronum's old CMake minimums. So enumerate the transitive static libs by hand
# (MSVC resolves libs order-independently, so a flat list is fine), add the static-lib defines
# (AL_LIBTYPE_STATIC / GLEW_STATIC -- without them the headers declare dllimport symbols the static
# libs don't provide), and add the Win32 system libs the static codecs + OpenSSL pull in.
# [rc4l] Full audio static-lib set (verified against the vcpkg static-md tree). libsndfile pulls
# FLAC/ogg/vorbis; its MP3 support pulls lame (libmp3lame-static/libmpghip-static); mpg123 has its
# out123/syn123 companions. MSVC links order-independently and drops any lib nothing references, so
# an over-complete bucket is safe and saves CI round-trips.
# fmt is a static dep of recent openal-soft (its logging uses fmt::v12).
$sndfileLibs = @("sndfile","FLAC","FLAC++","ogg","vorbis","vorbisenc","vorbisfile",
                 "opus","mpg123","out123","syn123","libmp3lame-static","libmpghip-static","fmt") |
               ForEach-Object { "$dep/lib/$_.lib" }
# avrt.lib: openal-soft's WASAPI backend uses the MMCSS AvSetMmThreadCharacteristics APIs.
$sysLibs     = @("crypt32.lib","ws2_32.lib","bcrypt.lib","advapi32.lib","user32.lib",
                 "shlwapi.lib","avrt.lib","opengl32.lib","glu32.lib")
# [rc4l] Diagnostic: static-triplet lib names differ from dynamic; dump the real names so a mismatch
# is one glance, not one CI cycle.
Write-Note ("Static libs in $dep\lib:`n  " + (((Get-ChildItem "$dep\lib" -Filter *.lib -ErrorAction SilentlyContinue).Name | Sort-Object) -join "`n  "))
# vcpkg names the static GLEW lib libglew32.lib (not glew32.lib) -- resolve by glob (matches the
# 'lib' prefix too).
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
& cmake -S (Join-Path $ScriptRoot "src\zandronum") -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T v143 `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
    -DNO_FMOD=ON -DNO_OPENAL=OFF `
    -DFORCE_INTERNAL_JPEG=ON -DFORCE_INTERNAL_BZIP2=ON -DFORCE_INTERNAL_ZLIB=ON `
    -DFORCE_INTERNAL_GME=ON `
    "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /DAL_LIBTYPE_STATIC /DGLEW_STATIC" `
    "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /DAL_LIBTYPE_STATIC /DGLEW_STATIC" `
    "-DOPENAL_INCLUDE_DIR=$dep/include/AL" `
    "-DOPENAL_LIBRARY=$dep/lib/OpenAL32.lib" `
    "-DSNDFILE_INCLUDE_DIR=$dep/include" `
    "-DSNDFILE_LIBRARY=$($sndfileLibs -join ';')" `
    "-DMPG123_INCLUDE_DIR=$dep/include" `
    "-DMPG123_LIBRARIES=$dep/lib/mpg123.lib" `
    "-DOPUS_INCLUDE_DIR=$dep/include/opus" `
    "-DOPUS_LIBRARIES=$dep/lib/opus.lib" `
    "-DGLEW_INCLUDE_DIR=$dep/include" `
    "-DGLEW_LIBRARY=$($glewLib.FullName)" `
    "-DOPENSSL_ROOT_DIR=$dep" "-DOPENSSL_USE_STATIC_LIBS=ON" `
    "-DCMAKE_EXE_LINKER_FLAGS=$($sysLibs -join ' ')" `
    @symArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

# --- Build -----------------------------------------------------------------
Write-Status "Building ($Configuration)"
& cmake --build $BuildDir --config $Configuration -- -m
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$exe = Join-Path $BuildDir "$Configuration\zandronum.exe"
if (-not (Test-Path $exe)) { throw "zandronum.exe missing — the build failed" }
Write-Status "Compiled: $exe"

if ($NoPackage) {
    Write-Status "Done (compile only; -NoPackage)"
    return
}

# --- Package (mirrors CI so a dev build matches a release) ------------------
Write-Status "Packaging dist-windows/"
$out = Join-Path $BuildDir $Configuration
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item "$out\zandronum.exe" $DistDir\
Copy-Item "$out\*.pk3" $DistDir\ -ErrorAction SilentlyContinue

# [rc4l] Ship Freedoom so the zip is playable without a separate IWAD (BSD-3-clause, clause 2
# requires the notice to accompany binary distributions).
if (Test-Path (Join-Path $ScriptRoot "tools\freedoom\freedoom2.wad")) {
    Copy-Item (Join-Path $ScriptRoot "tools\freedoom\*.wad") $DistDir\
    Copy-Item (Join-Path $ScriptRoot "tools\freedoom\License.txt") "$DistDir\FREEDOOM-LICENSE.txt"
} else {
    throw "tools/freedoom/freedoom2.wad missing — the zip would ship without a game"
}

# [rc4l] GPL-3.0 sections 4-6: the binary must carry the license text and point at the source.
Copy-Item (Join-Path $ScriptRoot "LICENSE.txt") $DistDir\
Copy-Item (Join-Path $ScriptRoot "THIRD-PARTY-NOTICES.txt") $DistDir\

# [rc4l] Static build: the audio/GL stack is linked INTO zandronum.exe, so there are no codec DLLs
# to ship. Verify statically -- the exe must NOT import OpenAL32.dll (that would mean we accidentally
# fell back to the dynamic lib), yet the link succeeded, which means the static OpenAL is in. Also
# copy any DLLs that genuinely remained (should be none beyond system) so a stray runtime dep can't
# silently break the package.
# dumpbin needs the VS dev environment, which isn't on the plain PowerShell PATH -- resolve it from
# the VS install (best-effort). The link succeeding against the static OpenAL lib is the real proof;
# this is a belt-and-suspenders check that we didn't silently fall back to the dynamic lib.
$dumpbin = $null
$dbCmd = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($dbCmd) { $dumpbin = $dbCmd.Source }
if (-not $dumpbin) {
    $dbFile = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($dbFile) { $dumpbin = $dbFile.FullName }
}
if ($dumpbin) {
    $deps = & $dumpbin /dependents $exe 2>$null | Select-String -Pattern '\.dll' | ForEach-Object { $_.ToString().Trim() }
    Write-Note ("exe DLL dependents:`n  " + (($deps -join "`n  ")))
    if ($deps -match '(?i)OpenAL32\.dll') {
        throw "zandronum.exe still imports OpenAL32.dll — static OpenAL link did not take"
    }
    Write-Note "sound OK: OpenAL is statically linked (no OpenAL32.dll dependency)"
} else {
    Write-Note "dumpbin not found; skipping the static-dependency check (the static link already succeeded)"
}
# Static build: no codec DLLs in the tree, but copy any that remain so a stray dep can't be missed.
Copy-Item "$VcpkgInstalled\bin\*.dll" $DistDir\ -ErrorAction SilentlyContinue

$zip = Join-Path $ScriptRoot "ZandroX-$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$DistDir\*" -DestinationPath $zip -Force
Write-Status "Done: $zip"
