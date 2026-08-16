# [rc4l] Launch the dist-mine engine for automated testing: hands off the human's keyboard and mouse.
#
# An agent-driven instance sharing a desktop with somebody working is a nuisance in a specific way:
# the window takes focus, and physical input lands in whichever engine happens to be frontmost. That
# happened -- a test instance was catching mouse movement while the user was doing something else.
#
# Three separate things have to be off, because each covers a different path:
#   ZANDRONUM_BRIDGE_INPUT_LOCK  drops OS input at the WndProc hook AND the DirectInput devices
#   use_mouse 0                  the engine's own mouse grab, which the lock does not own
#   use_joystick 0               same, for pads
#
# Everything the harness needs still arrives over the bridge, which is not affected by any of these.
param(
    [string]$Map  = "MAP01",
    [int]$Port    = 41900,
    [string]$Token = "mine",
    [string]$File  = "",
    [string]$Iwad  = "doom2.wad",
    [int]$Strength = -1
)

$ErrorActionPreference = "Stop"
$mine = "F:\ZandroX\.claude\worktrees\browser-join-server\dist-mine"

# Only ever kills instances launched from dist-mine. The human's copy runs from dist-windows and is
# never touched: killing it mid-session lost their place several times before this existed.
Get-Process forkundera -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "*dist-mine*" } |
    ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Seconds 1

$env:ZANDRONUM_BRIDGE_PORT = "$Port"
$env:ZANDRONUM_BRIDGE_TOKEN = $Token
$env:ZANDRONUM_BRIDGE_INPUT_LOCK = "1"

$args = @("-iwad", $Iwad)
if ($File -ne "") { $args += @("-file", $File) }
$args += @(
    "+map", $Map,
    "+set", "use_mouse", "0",
    "+set", "use_joystick", "0",
    "+set", "snd_musicvolume", "0",
    "+set", "snd_sfxvolume", "0",
    "+set", "snd_menuvolume", "0",
    "+set", "vid_defwidth", "640",
    "+set", "vid_defheight", "400",
    "+set", "fullscreen", "0",
    "+sv_fua_friendlymonsters", "1",
    "+cl_fua_skytint", "1"
)
if ($Strength -ge 0) { $args += @("+cl_fua_skytint_strength", "$Strength") }

Start-Process -FilePath "$mine\forkundera.exe" -WorkingDirectory $mine -ArgumentList $args
Start-Sleep -Seconds 9
$n = (Get-Process forkundera -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*dist-mine*" } | Measure-Object).Count
Write-Host "==> dist-mine engine on port $Port ($n running), input locked"
