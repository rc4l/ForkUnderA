#Requires -Version 5.1
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

<#
.SYNOPSIS
    Fast Windows build of ZandroX using the prebuilt deps in windows_assets/.

.DESCRIPTION
    Same output as windows_compile.ps1 — a packaged dist-windows/ zip — but it skips
    vcpkg entirely and links the OpenAL audio stack straight from the committed
    windows_assets/ folder (headers, import libs, runtime DLLs). No ~15-minute
    dependency build, no network: point it at Visual Studio 2022 and go.

    Use windows_compile.ps1 instead when you need to rebuild the dependencies from
    source (e.g. to refresh windows_assets/ — see windows_assets/README.md).

    De-Zandronum principle — ZandroX, not upstream Zandronum: OpenAL only (never FMOD),
    builds the in-repo src/zandronum.

.PARAMETER Configuration
    Debug or Release (default: Release).

.PARAMETER Version
    Version string baked into the zip name (default: dev-<short git sha>).

.PARAMETER Clean
    Remove the build and dist directories before building.

.PARAMETER NoPackage
    Stop after compiling; do not assemble or zip dist-windows/.

.EXAMPLE
    .\windows_build.ps1
    .\windows_build.ps1 -Clean -Version v0.2.0
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$Version = "",

    [switch]$Clean,
    [switch]$NoPackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptRoot = $PSScriptRoot
$BuildDir   = Join-Path $ScriptRoot "build-win"
$DistDir    = Join-Path $ScriptRoot "dist-windows"
$Deps       = Join-Path $ScriptRoot "windows_assets"

function Write-Status { param([string]$Message) Write-Host "==> $Message" -ForegroundColor Green }
function Write-Note   { param([string]$Message) Write-Host "    $Message" -ForegroundColor DarkGray }

function Get-DefaultVersion {
    try {
        $sha = (& git -C $ScriptRoot rev-parse --short=8 HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $sha) { return "dev-$sha" }
    } catch { }
    return "dev"
}

# [rc4l] The whole point of this script is the committed deps; fail clearly if they are missing.
if (-not (Test-Path (Join-Path $Deps "lib\OpenAL32.lib"))) {
    throw "windows_assets/ is missing or incomplete (no lib/OpenAL32.lib). Use windows_compile.ps1, or regenerate windows_assets/ — see windows_assets/README.md."
}
# [rc4l] GLEW is mandatory for every client build since d2e4479; without it CMake fails at configure
# with a bare GLEW_INCLUDE_DIR-NOTFOUND. Check it here so the error names the actual cause.
if (-not (Test-Path (Join-Path $Deps "lib\glew32.lib"))) {
    throw "windows_assets/ has no lib/glew32.lib. GLEW is required for the client build — regenerate windows_assets/ with the windows-export-deps workflow (it installs glew:x64-windows)."
}
# [rc4l] FFmpeg is checked here rather than left to CMake because CMake does NOT fail without it —
# src/CMakeLists.txt treats it as optional and compiles instant replay as a no-capture stub. So a
# windows_assets/ without ffmpeg configures, builds, packages and ships a binary that answers the
# clip key with "not built into this binary", and nothing along the way says so. That is exactly how
# it shipped, which is why the check is a hard failure and not a warning.
if (-not (Test-Path (Join-Path $Deps "lib\avcodec.lib"))) {
    throw "windows_assets/ has no lib/avcodec.lib. FFmpeg is required for instant replay — regenerate windows_assets/ with the windows-export-deps workflow (it installs ffmpeg[x264]:x64-windows)."
}

if ($Clean) {
    Write-Status "Cleaning build and dist directories"
    foreach ($d in @($BuildDir, $DistDir)) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
}

if (-not $Version) { $Version = Get-DefaultVersion }
Write-Status "ZandroX fast Windows build — configuration=$Configuration version=$Version"

# [rc4l] Visual Studio ships its own cmake and does NOT put it on PATH, so a machine with a perfectly
# good C++ toolchain fails here with "install CMake" and sends you installing something you already
# have. Same resolver as windows_build_run.ps1 -- keep the two in step.
function Resolve-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidates = @()
    foreach ($pf in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $pf) { continue }
        $candidates += (Join-Path $pf "CMake\bin\cmake.exe")
        foreach ($ed in @("Community", "Professional", "Enterprise", "BuildTools")) {
            $candidates += (Join-Path $pf "Microsoft Visual Studio\2022\$ed\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
        }
    }
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    throw "cmake not found on PATH or in any Visual Studio 2022 install. Install CMake, or run from a Developer PowerShell."
}
$CMake = Resolve-CMake
Write-Note "cmake: $CMake"

# --- DirectX headers/libs from the Windows SDK -----------------------------
# [rc4l] Zandronum's build wants the legacy DirectX SDK layout; reshape it out of the modern SDK.
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

# --- Configure (MSVC x64, NO_FMOD, OpenAL) — deps from windows_assets/ ------
# [rc4l] Drop out of "Stop" for the two native calls below. Under $ErrorActionPreference = "Stop",
# PowerShell wraps every stderr line from a native exe in a NativeCommandError and treats it as
# TERMINATING — so a CMake *warning* (find_package dev warnings, which src/CMakeLists.txt emits on a
# clean configure) aborts the script as though the build had failed, with a message that names the
# warning rather than the abort. The exit code is what actually says whether it worked, and both
# calls check $LASTEXITCODE immediately.
$PrevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"

Write-Status "Configuring CMake (Visual Studio 2022, x64, OpenAL, prebuilt deps)"
# [rc4l] FFmpeg is pinned by absolute path like every other dependency here, and CMAKE_PREFIX_PATH is
# a backstop rather than the mechanism. src/CMakeLists.txt locates FFmpeg with a bare
# find_path/find_library, which does two things we do not want: it searches wherever it likes, and it
# SKIPS ENTIRELY when the cache already holds a value. So a build directory previously configured by
# windows_compile.ps1 keeps pointing at that script's vcpkg tree -- the fast path then compiles
# against a static FFmpeg while linking everything else dynamically, which is a real failure this
# very tree produced. Naming the five variables the fallback uses makes the answer come from
# windows_assets/ and nowhere else, stale cache or not.
#
# [rc4l] The configure log is kept so the replay assertion below has something to read. CMake reports
# which optional features it turned on, and that report is the only place the difference between a
# full build and a feature-stripped one is visible.
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$ConfigureLog = Join-Path $BuildDir "configure.log"
& $CMake -S (Join-Path $ScriptRoot "src\zandronum") -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T v143 `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
    "-DCMAKE_PREFIX_PATH=$Deps" `
    -DNO_FMOD=ON -DNO_OPENAL=OFF `
    -DFORCE_INTERNAL_JPEG=ON -DFORCE_INTERNAL_BZIP2=ON -DFORCE_INTERNAL_ZLIB=ON `
    -DFORCE_INTERNAL_GME=ON `
    "-DOPENAL_INCLUDE_DIR=$Deps/include/AL" `
    "-DOPENAL_LIBRARY=$Deps/lib/OpenAL32.lib" `
    "-DSNDFILE_INCLUDE_DIR=$Deps/include" `
    "-DSNDFILE_LIBRARY=$Deps/lib/sndfile.lib" `
    "-DMPG123_INCLUDE_DIR=$Deps/include" `
    "-DMPG123_LIBRARIES=$Deps/lib/mpg123.lib" `
    "-DOPUS_INCLUDE_DIR=$Deps/include/opus" `
    "-DOPUS_LIBRARIES=$Deps/lib/opus.lib" `
    "-DGLEW_INCLUDE_DIR=$Deps/include" `
    "-DGLEW_LIBRARY=$Deps/lib/glew32.lib" `
    "-DFFMPEG_INCLUDE_DIRS=$Deps/include" `
    "-DFFMPEG_AVCODEC_LIB=$Deps/lib/avcodec.lib" `
    "-DFFMPEG_AVFORMAT_LIB=$Deps/lib/avformat.lib" `
    "-DFFMPEG_AVUTIL_LIB=$Deps/lib/avutil.lib" `
    "-DFFMPEG_SWSCALE_LIB=$Deps/lib/swscale.lib" `
    "-DOPENSSL_ROOT_DIR=$Deps" "-DOPENSSL_USE_STATIC_LIBS=OFF" | Tee-Object -FilePath $ConfigureLog
if ($LASTEXITCODE -ne 0) { $ErrorActionPreference = $PrevEAP; throw "cmake configure failed" }

# [rc4l] Belt and braces over the avcodec.lib check above: the libs can be present and CMake still
# not enable replay (missing headers, a find_path that lands somewhere else). The lib check catches
# the deps being absent; this catches them being unusable, and the two failure modes look identical
# from the outside -- a binary that builds and cannot record.
if (-not (Select-String -Path $ConfigureLog -Pattern "FUA replay: FFmpeg found" -Quiet)) {
    $ErrorActionPreference = $PrevEAP
    throw "CMake did not enable instant replay (no 'FUA replay: FFmpeg found' in $ConfigureLog). The build would ship a no-capture stub — check windows_assets/include/libavcodec."
}

Write-Status "Building ($Configuration)"
& $CMake --build $BuildDir --config $Configuration -- -m
$BuildExit = $LASTEXITCODE
$ErrorActionPreference = $PrevEAP
if ($BuildExit -ne 0) { throw "cmake build failed" }

$exe = Join-Path $BuildDir "$Configuration\forkundera.exe"
if (-not (Test-Path $exe)) { throw "forkundera.exe missing — the build failed" }
Write-Status "Compiled: $exe"

if ($NoPackage) { Write-Status "Done (compile only; -NoPackage)"; return }

# --- Package ---------------------------------------------------------------
Write-Status "Packaging dist-windows/"
$out = Join-Path $BuildDir $Configuration
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item "$out\forkundera.exe" $DistDir\
Copy-Item "$out\*.pk3" $DistDir\ -ErrorAction SilentlyContinue

if (Test-Path (Join-Path $ScriptRoot "tools\freedoom\freedoom2.wad")) {
    Copy-Item (Join-Path $ScriptRoot "tools\freedoom\*.wad") $DistDir\
    Copy-Item (Join-Path $ScriptRoot "tools\freedoom\License.txt") "$DistDir\FREEDOOM-LICENSE.txt"
} else {
    throw "tools/freedoom/freedoom2.wad missing — the zip would ship without a game"
}

Copy-Item (Join-Path $ScriptRoot "LICENSE.txt") $DistDir\
Copy-Item (Join-Path $ScriptRoot "THIRD-PARTY-NOTICES.txt") $DistDir\

# Runtime DLLs (openal-soft + decoders) straight from the committed deps.
Copy-Item "$Deps\bin\*.dll" $DistDir\ -ErrorAction SilentlyContinue
if (-not (Test-Path "$DistDir\OpenAL32.dll")) {
    throw "OpenAL32.dll missing from dist-windows — the build would ship without sound"
}
Write-Note "sound OK: OpenAL32.dll present"

# [rc4l] The engine links avcodec by import lib, so a zip without the DLL beside the exe is one that
# will not start at all -- a louder failure than the stub, but still one to catch before shipping.
# Versioned name (avcodec-61.dll and friends), so match by pattern rather than by version.
if (-not (Get-ChildItem "$DistDir\avcodec-*.dll" -ErrorAction SilentlyContinue)) {
    throw "no avcodec-*.dll in dist-windows — the build would ship unable to start; check windows_assets/bin"
}
Write-Note "instant replay OK: FFmpeg runtime present"

$zip = Join-Path $ScriptRoot "ForkUnderA-$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$DistDir\*" -DestinationPath $zip -Force
Write-Status "Done: $zip"
