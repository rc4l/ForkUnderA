# Starts a program without activating its window (SW_SHOWNOACTIVATE), in a working directory of its choice, and waits for it.
# Prints "exit 0" when it quit, "timeout" when it was stopped after -TimeoutMs, "failed" when it couldn't start.
# usage: powershell -NoProfile -File uzdoom-launch.ps1 -Exe <program> -Dir <working dir> [-TimeoutMs 60000], the whole command line in $env:FUACTL_LAUNCH_CMDLINE
# (an argument would lose its quotes on the way through powershell -File).
param([string]$Exe, [string]$Dir, [int]$TimeoutMs = 60000)
$CommandLine = $env:FUACTL_LAUNCH_CMDLINE

# A stale LIB naming a removed MSVC folder fails Add-Type's compile, so this process drops it.
$env:LIB = $null

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class NoFocusLaunch {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO { public int cb; public string r1, desk, title; public int x, y, w, h, cx, cy, fill, flags; public short show, r2; public IntPtr r3, hin, hout, herr; }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int pid, tid; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, int flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, uint code);
    public static string Run(string app, string cmd, string dir, uint timeoutMs) {
        STARTUPINFO si = new STARTUPINFO(); si.cb = Marshal.SizeOf(si); si.flags = 1; si.show = 4;
        PROCESS_INFORMATION pi;
        if (!CreateProcess(app, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, dir, ref si, out pi)) return "failed";
        if (WaitForSingleObject(pi.hProcess, timeoutMs) != 0) { TerminateProcess(pi.hProcess, 1); return "timeout"; }
        return "exit 0";
    }
}
"@

New-Item -ItemType Directory -Force $Dir | Out-Null
Write-Output ([NoFocusLaunch]::Run($Exe, $CommandLine, $Dir, [uint32]$TimeoutMs))
