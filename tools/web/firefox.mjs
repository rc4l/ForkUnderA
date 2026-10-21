// [rc4l] smoke's run in Firefox, driven over WebDriver BiDi: headless Firefox offers no WebGPU adapter, so this one opens a window, placed off the screen and muted.
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { smoke, retry, removeQuietly } from './smoke.mjs';

export const FIREFOX_CANDIDATES = [
  'C:/Program Files/Mozilla Firefox/firefox.exe',
  '/Applications/Firefox.app/Contents/MacOS/firefox',
  '/usr/bin/firefox',
];

// [rc4l] A fresh profile's window (off the screen, where it neither covers nor takes the player's desktop) and settings (not throttled for being out of sight, silent, audio free to start when autoplay asks).
export function firefoxProfile(dir, { autoplay = false } = {}) {
  const window = { screenX: '-4000', screenY: '40', width: '1280', height: '800', sizemode: 'normal' };
  fs.writeFileSync(path.join(dir, 'xulstore.json'), JSON.stringify({ 'chrome://browser/content/browser.xhtml': { 'main-window': window } }));
  const prefs = {
    'widget.windows.window_occlusion_tracking.enabled': false,
    'media.volume_scale': '0.0',
    'media.autoplay.default': autoplay ? 0 : 1,
  };
  fs.writeFileSync(path.join(dir, 'user.js'), Object.entries(prefs).map(([k, v]) => `user_pref(${JSON.stringify(k)}, ${JSON.stringify(v)});`).join('\n') + '\n');
}

// [rc4l] The script that moves process pid's visible windows off every screen, since Firefox brings a saved position back onto one, printing "MOVED|n".
export function offScreenScript(pid) {
  return `Add-Type @"
using System; using System.Runtime.InteropServices;
public class FuaFox {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
}
"@
$n = 0
[void][FuaFox]::EnumWindows({ param($h, $l)
  $p = [uint32]0; [void][FuaFox]::GetWindowThreadProcessId($h, [ref]$p)
  if ($p -eq ${Number(pid)} -and [FuaFox]::IsWindowVisible($h)) { $script:n += [int][FuaFox]::MoveWindow($h, -4000, 40, 1280, 800, $true) }
  return $true }, [IntPtr]::Zero)
"MOVED|$n"
`;
}

// [rc4l] A BiDi log entry as smoke's console line: a console call by its method ("[log] text"), a thrown error as "[exception] text".
export function formatLogEntry(p) {
  if (p.type === 'console') return `[${p.method || p.level}] ${p.text}`;
  return `[exception] ${p.text}`;
}

// [rc4l] A BiDi evaluation's result as smoke's eval line, the page's value carried as the JSON the expression was wrapped to return.
export function formatBidiEval(r) {
  if (!r || r.type === 'exception') return `[eval] threw ${(r && r.exceptionDetails && r.exceptionDetails.text) || 'unknown'}`;
  return `[eval] ${r.result && r.result.type === 'string' ? r.result.value : 'null'}`;
}

// [rc4l] The run a browser's pages get, smoke for Chrome and smokeFirefox for Firefox; any other name is a mistake worth stopping on.
export function smokeIn(browser) {
  if (browser === 'chrome') return smoke;
  if (browser === 'firefox') return smokeFirefox;
  throw new Error(`no driver for the browser "${browser}": chrome or firefox`);
}

// [rc4l] What smoke takes and gives, for the options a probe uses: init before the page's scripts, evaluate `after` seconds in, until the first line matching until.
export async function smokeFirefox({ url, until, seconds = 30, firefox, autoplay = false, onLine = console.log, evaluate, after = 10, init }) {
  const exe = firefox ?? FIREFOX_CANDIDATES.find((p) => fs.existsSync(p));
  if (!exe) throw new Error('no Firefox found');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-firefox-'));
  firefoxProfile(dir, { autoplay });
  const port = 9300 + Math.floor(Math.random() * 600);
  const proc = spawn(exe, ['-no-remote', '-profile', dir, '--remote-debugging-port', String(port)], { stdio: 'ignore' });
  const lines = [];
  try {
    const deadline = Date.now() + seconds * 1000;
    const ws = await retry(() => new Promise((res, rej) => { const s = new WebSocket(`ws://127.0.0.1:${port}/session`); s.onopen = () => res(s); s.onerror = rej; }), deadline);
    let id = 0, matched = null, wake = () => {};
    const pending = new Map();
    const take = (line) => {
      lines.push(line);
      onLine(line);
      if (until && new RegExp(until).test(line)) { matched = line; wake(); }
    };
    ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
      if (m.method === 'log.entryAdded') take(formatLogEntry(m.params));
    };
    const send = (method, params = {}) => new Promise((res) => { const n = ++id; pending.set(n, res); ws.send(JSON.stringify({ id: n, method, params })); });
    const version = `Firefox ${(await send('session.new', { capabilities: {} })).result?.capabilities?.browserVersion}`;
    if (process.platform === 'win32') spawnSync('powershell', ['-NoProfile', '-Command', offScreenScript(proc.pid)], { stdio: 'ignore' });
    await send('session.subscribe', { events: ['log.entryAdded'] });
    if (init) await send('script.addPreloadScript', { functionDeclaration: `() => { ${init} }` });
    const context = (await send('browsingContext.getTree', {})).result.contexts[0].context;
    await send('browsingContext.navigate', { context, url, wait: 'none' });
    if (evaluate) {
      setTimeout(async () => {
        const r = await send('script.evaluate', { expression: `(async () => JSON.stringify(await (${evaluate})))()`, target: { context }, awaitPromise: true });
        take(formatBidiEval(r.result));
      }, Number(after) * 1000).unref();
    }
    await new Promise((res) => { wake = res; setTimeout(res, Math.max(0, deadline - Date.now())); });
    ws.close();
    return { matched, lines, version };
  } finally {
    // [rc4l] Its content and GPU processes go with it, and only this Firefox's: the player's own runs on another profile.
    if (process.platform === 'win32') spawnSync('taskkill', ['/pid', String(proc.pid), '/T', '/F'], { stdio: 'ignore' });
    else proc.kill();
    await new Promise((r) => setTimeout(r, 800));
    removeQuietly(dir);
  }
}
