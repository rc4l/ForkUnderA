#Requires -Version 5.1
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
$VcpkgRoot      = (Resolve-Vcpkg | Select-Object -Last 1)
$VcpkgExe       = Join-Path $VcpkgRoot "vcpkg.exe"
$VcpkgInstalled = Join-Path $VcpkgRoot "installed\x64-windows"

# --- Dependencies (OpenAL stack — never FMOD) ------------------------------
if ($SkipDeps) {
    Write-Status "Skipping vcpkg install (-SkipDeps)"
} else {
    Write-Status "Installing OpenAL audio dependencies via vcpkg (first run is slow)"
    & $VcpkgExe install `
        openal-soft:x64-windows libsndfile:x64-windows mpg123:x64-windows `
        opus:x64-windows openssl:x64-windows
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
Write-Status "Configuring CMake (Visual Studio 2022, x64, OpenAL)"
$dep = $VcpkgInstalled
& cmake -S (Join-Path $ScriptRoot "src\zandronum") -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T v143 `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
    -DNO_FMOD=ON -DNO_OPENAL=OFF `
    -DFORCE_INTERNAL_JPEG=ON -DFORCE_INTERNAL_BZIP2=ON -DFORCE_INTERNAL_ZLIB=ON `
    -DFORCE_INTERNAL_GME=ON `
    "-DOPENAL_INCLUDE_DIR=$dep/include/AL" `
    "-DOPENAL_LIBRARY=$dep/lib/OpenAL32.lib" `
    "-DSNDFILE_INCLUDE_DIR=$dep/include" `
    "-DSNDFILE_LIBRARY=$dep/lib/sndfile.lib" `
    "-DMPG123_INCLUDE_DIR=$dep/include" `
    "-DMPG123_LIBRARIES=$dep/lib/mpg123.lib" `
    "-DOPUS_INCLUDE_DIR=$dep/include/opus" `
    "-DOPUS_LIBRARIES=$dep/lib/opus.lib" `
    "-DOPENSSL_ROOT_DIR=$dep" "-DOPENSSL_USE_STATIC_LIBS=OFF"
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

# Runtime DLLs (openal-soft + decoders) next to the exe.
Copy-Item "$VcpkgInstalled\bin\*.dll" $DistDir\ -ErrorAction SilentlyContinue
if (-not (Test-Path "$DistDir\OpenAL32.dll")) {
    throw "OpenAL32.dll missing from dist-windows — the build would ship without sound"
}
Write-Note "sound OK: OpenAL32.dll present"

$zip = Join-Path $ScriptRoot "ZandroX-$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$DistDir\*" -DestinationPath $zip -Force
Write-Status "Done: $zip"
