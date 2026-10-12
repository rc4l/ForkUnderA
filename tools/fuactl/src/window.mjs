// [rc4l] The engine's top-level windows as Windows sees them, and what one really shows on screen, through user32 from PowerShell (Windows only).
import { spawnSync } from "node:child_process";

// [rc4l] Which renderer a window belongs to, by its title: GL's game window, the backend's, or neither.
export function windowKind(title) {
  const t = String(title);
  if (t.startsWith("Fua v")) return "gl";
  if (t.startsWith("ForkUnderA - Diligent")) return "vk";
  return null;
}

// [rc4l] The listing script's lines, "hwnd|pid|minimized|hung|left,top,right,bottom|title", as records with their kind; anything else is skipped.
export function parseWindowLines(text) {
  const out = [];
  for (const line of String(text).split(/\r?\n/)) {
    const f = line.split("|");
    if (f.length < 6 || !/^\d+$/.test(f[0])) continue;
    const [left, top, right, bottom] = f[4].split(",").map(Number);
    const title = f.slice(5).join("|");
    out.push({ hwnd: f[0], pid: Number(f[1]), minimized: f[2] === "1", hung: f[3] === "1",
      rect: { left, top, right, bottom }, title, kind: windowKind(title) });
  }
  return out;
}

const USER32 = `Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class FuaWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@`;

// [rc4l] The script that prints every visible top-level window of the engine processes, one line each.
export function listScript() {
  return `${USER32}
$pids = @(Get-Process -Name forkundera -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
$lines = New-Object System.Collections.Generic.List[string]
[void][FuaWin]::EnumWindows({ param($h, $l)
  $p = [uint32]0; [void][FuaWin]::GetWindowThreadProcessId($h, [ref]$p)
  if ($pids -contains $p -and [FuaWin]::IsWindowVisible($h)) {
    $sb = New-Object System.Text.StringBuilder 256; [void][FuaWin]::GetWindowText($h, $sb, 256)
    $r = New-Object FuaWin+RECT; [void][FuaWin]::GetWindowRect($h, [ref]$r)
    $lines.Add(("{0}|{1}|{2}|{3}|{4},{5},{6},{7}|{8}" -f $h, $p, [int][FuaWin]::IsIconic($h), [int][FuaWin]::IsHungAppWindow($h), $r.L, $r.T, $r.R, $r.B, $sb))
  }
  return $true }, [IntPtr]::Zero)
$lines
`;
}

// [rc4l] The script that saves what window hwnd shows to out as a PNG, as the compositor has it rather than GL's own back buffer, printing "SHOT|ok|width|height".
export function shotScript(hwnd, out) {
  return `${USER32}
Add-Type -AssemblyName System.Drawing
$h = [IntPtr]${Number(hwnd)}
$r = New-Object FuaWin+RECT; [void][FuaWin]::GetClientRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R)), ([Math]::Max(1, $r.B))
$g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
$ok = [FuaWin]::PrintWindow($h, $hdc, 3); $g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save('${String(out).replace(/'/g, "''")}')
"SHOT|{0}|{1}|{2}" -f [int]$ok, $r.R, $r.B
`;
}

function runPowerShell(script) {
  const r = spawnSync("powershell", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script], { encoding: "utf8" });
  if (r.error) throw r.error;
  return r.stdout || "";
}

// [rc4l] The engine's windows, and with shot ("gl" or "vk") the first of that kind captured to out.
export function engineWindows({ shot = null, out = null } = {}) {
  if (process.platform !== "win32") throw new Error("fuactl window reads Windows' own window state, so it runs on Windows only");
  const windows = parseWindowLines(runPowerShell(listScript()));
  if (!shot) return { windows };
  const target = windows.find((w) => w.kind === shot);
  if (!target) return { windows, shot: null };
  const line = runPowerShell(shotScript(target.hwnd, out)).split(/\r?\n/).find((l) => l.startsWith("SHOT|")) || "";
  const [, ok, width, height] = line.split("|");
  return { windows, shot: { hwnd: target.hwnd, out, ok: ok === "1" && !target.minimized, width: Number(width), height: Number(height) } };
}
