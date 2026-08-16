# [rc4l] Stage a fresh build into dist-mine/, a private copy for automated testing.
#
# dist-windows/ is the directory a person launches from. Staging a build there means overwriting an
# exe that a running engine holds open, so an agent rebuilding while someone is playing either fails
# to stage or has to kill their session to proceed. Both happened repeatedly.
#
# dist-mine/ is a second staging directory with its own exe and pk3s, and a junction to Downloads so
# the wads are not duplicated. Builds land here; the human's copy is never touched.
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root  = "F:\ZandroX\.claude\worktrees\browser-join-server"
$build = "$root\build-win"
$mine  = "$root\dist-mine"
$src   = "$root\dist-windows"

if (-not $SkipBuild) {
    $cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    Write-Host "==> Building (all targets)"
    & $cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

if (-not (Test-Path $mine)) { New-Item -ItemType Directory $mine | Out-Null }

# The exe, plus the pk3s: those are rebuilt by add_pk3 and a stale one is the classic silent failure.
$exe = "$build\Release\forkundera.exe"
if (-not (Test-Path $exe)) { $exe = "$build\Release\zdoom.exe" }
if (-not (Test-Path $exe)) { throw "no exe in $build\Release" }
Copy-Item $exe "$mine\forkundera.exe" -Force

Get-ChildItem "$build" -Filter *.pk3 -Recurse -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName $mine -Force }

# Runtime deps and the IWAD come from the human's staging dir, copied once and left alone after.
foreach ($pat in @("*.dll", "doom2.wad")) {
    Get-ChildItem $src -Filter $pat -File -ErrorAction SilentlyContinue | ForEach-Object {
        $dst = Join-Path $mine $_.Name
        if (-not (Test-Path $dst)) { Copy-Item $_.FullName $dst -Force }
    }
}

# Junction rather than a copy: the wad collection is large and identical.
if (-not (Test-Path "$mine\Downloads")) {
    cmd /c mklink /J "$mine\Downloads" "$src\Downloads" | Out-Null
}

$stamp = (Get-Item "$mine\forkundera.exe").LastWriteTime
Write-Host "==> dist-mine staged, exe $stamp"
Write-Host "    launch it with tools/fuactl/run-mine.ps1 -Map <map> -Port <port>"
