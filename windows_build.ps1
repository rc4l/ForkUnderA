#Requires -Version 5.1
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

if ($Clean) {
    Write-Status "Cleaning build and dist directories"
    foreach ($d in @($BuildDir, $DistDir)) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
}

if (-not $Version) { $Version = Get-DefaultVersion }
Write-Status "ZandroX fast Windows build — configuration=$Configuration version=$Version"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found on PATH. Install CMake and Visual Studio 2022 (with the C++ workload)."
}

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
Write-Status "Configuring CMake (Visual Studio 2022, x64, OpenAL, prebuilt deps)"
& cmake -S (Join-Path $ScriptRoot "src\zandronum") -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T v143 `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
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
    "-DOPENSSL_ROOT_DIR=$Deps" "-DOPENSSL_USE_STATIC_LIBS=OFF"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Status "Building ($Configuration)"
& cmake --build $BuildDir --config $Configuration -- -m
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$exe = Join-Path $BuildDir "$Configuration\zandronum.exe"
if (-not (Test-Path $exe)) { throw "zandronum.exe missing — the build failed" }
Write-Status "Compiled: $exe"

if ($NoPackage) { Write-Status "Done (compile only; -NoPackage)"; return }

# --- Package ---------------------------------------------------------------
Write-Status "Packaging dist-windows/"
$out = Join-Path $BuildDir $Configuration
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item "$out\zandronum.exe" $DistDir\
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

$zip = Join-Path $ScriptRoot "ZandroX-$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$DistDir\*" -DestinationPath $zip -Force
Write-Status "Done: $zip"
