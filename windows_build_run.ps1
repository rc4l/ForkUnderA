#Requires -Version 5.1
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

<#
.SYNOPSIS
    Fail-CLOSED incremental rebuild + dist refresh for ZandroX (Windows).

.DESCRIPTION
    The Windows counterpart to mac_build_run.sh, and THE sanctioned way to rebuild
    and relaunch after a code or wadsrc/ change. It replaces hand-rolling
    `cmake --build --target zdoom` plus manual Copy-Item of the exe and pk3s, which
    silently ships STALE or MISSING artifacts and sends you debugging the wrong
    layer. Every step here fails LOUD instead:

      1. Builds EVERY target, not just zdoom. `--target zdoom` builds one target of
         several: forkundera-server-registry compiles src/gitinfo.cpp with its own
         source list, so a function added to a file the engine also compiles links
         fine in forkundera.exe and fails in a sibling that never listed the unit
         defining it. Iterating on `--target zdoom` will not notice, however many
         times you run it -- the sibling simply never relinks, and the first thing
         that tells you is red CI on all three platforms.
      2. Build fails, or produces no exe -> STOP. Never stage a stale binary.
      3. A pk3 is missing or older than    -> repack it with zipdir. A zdoom-only
         its wadsrc/ inputs                   build never rebuilds pk3s: add_pk3's
                                              only DEPENDS is the zipdir *tool*,
                                              not the wadsrc content, so they go
                                              stale, and a deleted pk3 never
                                              returns. A full build does run them
                                              (they are ALL targets) -- this check
                                              is the belt to that braces, and the
                                              only thing that catches a pk3 the
                                              build refreshed but never copied.
      4. dist ends up without zandronum.pk3, -> STOP. The engine aborts at startup
         or with an exe/pk3 that does not      with "Cannot find zandronum.pk3",
         match build-win/                      which reads like a data problem and
                                               is not one.

    Only when every artifact is proven fresh and consistent does it say dist is safe
    to launch. Requires dist-windows/ assembled once by .\windows_build.ps1 (runtime
    DLLs, freedoom wads, licences); this script only refreshes the exe + pk3s.

.PARAMETER Configuration
    Debug or Release (default: Release). Must match what windows_build.ps1 built.

.PARAMETER Run
    Launch dist-windows\forkundera.exe once everything verifies.

.PARAMETER ExtraArgs
    Arguments passed through to the engine when -Run is given.

.EXAMPLE
    .\windows_build_run.ps1
    .\windows_build_run.ps1 -Run
    .\windows_build_run.ps1 -Run -ExtraArgs '-iwad','freedoom2.wad','+map','map01'
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [switch]$Run,

    [string[]]$ExtraArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptRoot = $PSScriptRoot
$BuildDir   = Join-Path $ScriptRoot "build-win"
$DistDir    = Join-Path $ScriptRoot "dist-windows"
$SrcDir     = Join-Path $ScriptRoot "src\zandronum"
$OutDir     = Join-Path $BuildDir $Configuration
$ZipDir     = Join-Path $BuildDir "tools\zipdir\$Configuration\zipdir.exe"
$Exe        = Join-Path $OutDir "forkundera.exe"

function Write-Status { param([string]$Message) Write-Host "==> $Message" -ForegroundColor Green }
function Write-Note   { param([string]$Message) Write-Host "    $Message" -ForegroundColor DarkGray }
function Write-Warn   { param([string]$Message) Write-Host "warning: $Message" -ForegroundColor Yellow }
function Die {
    param([string]$Message)
    Write-Host "BUILD-RUN FAILED: $Message" -ForegroundColor Red
    exit 1
}

# [rc4l] cmake is usually NOT on PATH here: the only copy on a stock machine ships
# inside Visual Studio, and it is on PATH only in a Developer PowerShell. Resolving it
# ourselves means this script runs from any shell -- the alternative is an unrelated
# "cmake is not recognized" that reads like a missing install and is not one.
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
    Die "cmake not found on PATH or in any Visual Studio 2022 install. Install CMake, or run from a Developer PowerShell."
}

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Die "build-win/ is not configured -- run .\windows_build.ps1 once first."
}
if (-not (Test-Path $ZipDir)) {
    Die "zipdir tool missing at $ZipDir -- run .\windows_build.ps1 once first."
}
if (-not (Test-Path $DistDir)) {
    Die "no dist-windows/ at $DistDir -- run .\windows_build.ps1 once to assemble it (DLLs, wads, licences)."
}

# --- 0. Refresh tags so the stamped version is accurate. -----------------------
# [rc4l] The build's version label comes from `git describe --tags --match "v*"`. A
# refspec pull does NOT fetch tags, so a checkout can be missing the release tags and
# stamp an OLD version onto CURRENT code -- which then makes the in-engine update
# checker falsely prompt "newer available". Best-effort: fast when up to date, a
# silent no-op offline.
& git -C $ScriptRoot fetch --tags --quiet origin "refs/tags/*:refs/tags/*" 2>$null | Out-Null

# --- 1. Build EVERY target. A failing or empty link is a HARD stop. ------------
# [rc4l] No --target here, deliberately, and this is the whole point of the script.
# See the header: zdoom is one target of several, and the ones you are not building
# are exactly the ones whose breakage you will not see until CI.
$CMake = Resolve-CMake
Write-Status "Building all targets ($Configuration, parallel)"
Write-Note "cmake: $CMake"

# [rc4l] Drop out of "Stop" for the native cmake call, the same way windows_build.ps1 does and for
# the same reason. Under $ErrorActionPreference = "Stop", ANY line a native program writes to stderr
# becomes a terminating error -- so a CMake *warning* aborted the script mid-build. It only shows up
# when cmake re-configures (a CMakeLists edit), which made it look intermittent and unrelated to the
# change that triggered it. The exit code is the thing that says whether the build failed, so that is
# what is checked.
$PrevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $CMake --build $BuildDir --config $Configuration -- -m
$BuildExit = $LASTEXITCODE
$ErrorActionPreference = $PrevEAP

if ($BuildExit -ne 0) {
    Die "the build failed (see the compile/link error above). NOT staging -- fix it; do not run a stale dist."
}
if (-not (Test-Path $Exe)) {
    Die "the build reported success but produced no exe at $Exe (silent link failure)."
}

# --- 2. pk3 freshness. Repack any pk3 that is missing or older than wadsrc/. ---
# [rc4l] wadsrc_lights is commented out of src/zandronum/CMakeLists.txt, so lights.pk3
# is deliberately absent -- do not add it here expecting a fourth file.
# [rc4l] The core pk3's name carries this build's release key, so it is discovered rather than
# named. Hard-coding it here would send the script hunting a file no build produces the moment the
# version moves. See src/zandronum/src/features/core-pk3.
$coreName = (Get-ChildItem -Path $OutDir -Filter "fua_core_*.pk3" -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1).Name
if (-not $coreName) {
    Die "build-win/ has no fua_core_*.pk3 -- the pk3 target did not run, so there is nothing to ship."
}

$pk3Pairs = @(
    @{ Dir = "wadsrc";     Name = $coreName },
    @{ Dir = "wadsrc_bm";  Name = "brightmaps.pk3" },
    @{ Dir = "wadsrc_st";  Name = "skulltag_actors.pk3" }
)
foreach ($pair in $pk3Pairs) {
    $staticDir = Join-Path $SrcDir "$($pair.Dir)\static"
    $pk3       = Join-Path $OutDir $pair.Name
    if (-not (Test-Path $staticDir)) { continue }

    $reason = ""
    if (-not (Test-Path $pk3)) {
        $reason = "missing"
    } else {
        $pk3Time = (Get-Item $pk3).LastWriteTimeUtc
        $newer = Get-ChildItem $staticDir -Recurse -File -ErrorAction SilentlyContinue |
                 Where-Object { $_.LastWriteTimeUtc -gt $pk3Time } |
                 Select-Object -First 1
        if ($newer) { $reason = "stale vs $($pair.Dir)/static" }
    }

    if ($reason) {
        Write-Status "Repacking $($pair.Name) ($reason)"
        & $ZipDir -udf $pk3 $staticDir | Out-Null
        if ($LASTEXITCODE -ne 0) { Die "zipdir failed to build $($pair.Name)." }
    }
    if (-not (Test-Path $pk3)) { Die "$($pair.Name) still absent after repack -- zipdir produced nothing." }
}

# --- 3. Sync into dist, VERIFY the copy is faithful. ---------------------------
Write-Status "Syncing fresh exe + pk3s into dist-windows/"
Copy-Item $Exe $DistDir -Force
foreach ($pk3 in (Get-ChildItem $OutDir -Filter *.pk3 -File)) {
    Copy-Item $pk3.FullName $DistDir -Force
}

# [rc4l] These are the assertions that catch a stale copy slipping through -- the
# failure mode where the build is fresh, the copy silently did not happen, and you
# spend an hour reading source that the running exe does not contain.
$distExe = Join-Path $DistDir "forkundera.exe"
$distPk3 = Join-Path $DistDir $coreName
if (-not (Test-Path $distPk3)) {
    Die "dist-windows/ has NO $coreName after sync -- the engine would abort with 'Cannot find $coreName'."
}

# [rc4l] Sweep the cores this build did not produce. They are inert, since the engine asks for an
# exact name, but a dist folder that grows a pk3 per version is how someone ends up shipping four.
Get-ChildItem -Path $DistDir -Filter "fua_core_*.pk3" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne $coreName } |
    ForEach-Object { Write-Note "removing stale core $($_.Name)"; Remove-Item $_.FullName -Force }

foreach ($pairToCheck in @(@($Exe, $distExe), @((Join-Path $OutDir $coreName), $distPk3))) {
    $a = (Get-FileHash $pairToCheck[0] -Algorithm SHA256).Hash
    $b = (Get-FileHash $pairToCheck[1] -Algorithm SHA256).Hash
    if ($a -ne $b) {
        Die "dist copy of $(Split-Path -Leaf $pairToCheck[1]) != build-win/ original (a stale copy slipped through)."
    }
}

# --- 3b. The catalogue, MIRRORED rather than copied. ---------------------------
# [rc4l] catalogue/ is source, not a build output, so nothing else was ever staging it: every run
# launched against whatever happened to be in dist-windows already. An addon.json edited here and a
# cfg renamed here both looked like they had taken and had not, which is the same silent wrong-data
# failure this script exists to catch for the pk3s.
#
# MIRRORED, because a plain copy only ever adds. A variant's cfg removed from source would linger in
# dist and keep working, so the one thing you cannot test is whether you have broken it -- and the
# engine's own check for a promised-but-missing cfg would keep passing on a file that is gone.
$catSrc = Join-Path $ScriptRoot "catalogue"
$catDst = Join-Path $DistDir "catalogue"

if (-not (Test-Path $catSrc)) {
    Write-Warn "no catalogue/ at $catSrc -- the HOST tab will have nothing to offer."
} else {
    if (-not (Test-Path $catDst)) { New-Item -ItemType Directory -Path $catDst | Out-Null }

    $srcFiles = @(Get-ChildItem $catSrc -Recurse -File)
    $srcRel   = @($srcFiles | ForEach-Object { $_.FullName.Substring($catSrc.Length).TrimStart('\') })

    foreach ($rel in $srcRel) {
        $to = Join-Path $catDst $rel
        $toDir = Split-Path $to -Parent
        if (-not (Test-Path $toDir)) { New-Item -ItemType Directory -Path $toDir -Force | Out-Null }
        Copy-Item (Join-Path $catSrc $rel) $to -Force
    }

    # Anything dist has that source does not is from an older layout. Removed rather than left,
    # for the reason above: a leftover cfg is a test that cannot fail.
    foreach ($stale in (Get-ChildItem $catDst -Recurse -File)) {
        $rel = $stale.FullName.Substring($catDst.Length).TrimStart('\')
        if ($srcRel -notcontains $rel) {
            Write-Note "removing stale catalogue file $rel"
            Remove-Item $stale.FullName -Force
        }
    }

    # Verified the same way the exe and the core pk3 are, and fails CLOSED for the same reason: a
    # catalogue that did not copy is a run against yesterday's entries.
    foreach ($rel in $srcRel) {
        $a = (Get-FileHash (Join-Path $catSrc $rel) -Algorithm SHA256).Hash
        $b = (Get-FileHash (Join-Path $catDst $rel) -Algorithm SHA256).Hash
        if ($a -ne $b) {
            Die "dist copy of catalogue/$rel != source (a stale copy slipped through)."
        }
    }

    Write-Note "catalogue: $($srcRel.Count) file(s) mirrored"
}

# [rc4l] windows_build.ps1 stages these once; warn rather than fail if dist predates a
# change to them, since neither is produced by this script.
if (-not (Test-Path (Join-Path $DistDir "OpenAL32.dll"))) {
    Write-Warn "dist-windows/ has no OpenAL32.dll -- the engine will run without sound. Re-run .\windows_build.ps1."
}
if (-not (Get-ChildItem $DistDir -Filter *.wad -File -ErrorAction SilentlyContinue)) {
    Write-Warn "dist-windows/ has no IWAD -- the engine will not start a game. Re-run .\windows_build.ps1."
}

Write-Status "OK -- fresh exe + pk3s verified in dist-windows/. Safe to launch."
Write-Note "exe: $distExe"

if ($Run) {
    Write-Status "Launching $distExe"
    if ($ExtraArgs.Count -gt 0) {
        Start-Process -FilePath $distExe -WorkingDirectory $DistDir -ArgumentList $ExtraArgs
    } else {
        Start-Process -FilePath $distExe -WorkingDirectory $DistDir
    }
}
