#!/usr/bin/env node
// [rc4l] Loads a page in headless Chrome with WebGPU, prints its console, and saves a screenshot once a line matches or time runs out.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const CHROME_CANDIDATES = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/usr/bin/google-chrome',
  '/usr/bin/chromium',
];

// [rc4l] Linux's Chrome without a GPU composites with GL, which has no texture it can share with WebGPU, so the canvas's swap textures could not be made and the device was lost; Chrome's own Vulkan and ANGLE go on SwiftShader there too.
const LINUX_SOFTWARE = ['--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-angle=swiftshader', '--disable-vulkan-surface', '--disable-dev-shm-usage'];

// [rc4l] `software` puts WebGPU on SwiftShader, Chrome's CPU renderer, for a machine with no GPU such as a CI runner; `autoplay` lets the page's audio run without a click, always muted, since a headless Chrome still plays to the machine's speakers.
export function chromeArgs(port, profileDir, { headless = true, software = false, autoplay = false, platform = process.platform } = {}) {
  return [
    ...(headless ? ['--headless=new'] : []),
    '--mute-audio',
    ...(autoplay ? ['--autoplay-policy=no-user-gesture-required'] : []),
    ...(software ? ['--use-webgpu-adapter=swiftshader', '--enable-unsafe-swiftshader', ...(platform === 'linux' ? LINUX_SOFTWARE : [])] : []),
    `--remote-debugging-port=${port}`,
    `--user-data-dir=${profileDir}`,
    '--enable-unsafe-webgpu',
    '--no-first-run',
    '--no-default-browser-check',
    '--window-size=1280,900',
    'about:blank',
  ];
}

// [rc4l] One console line from a CDP event: the level and its arguments as text, the way the page printed them.
export function formatConsole(method, params) {
  if (method === 'Runtime.consoleAPICalled') {
    const text = (params.args ?? []).map(a => a.value ?? a.description ?? a.type).join(' ');
    return `[${params.type}] ${text}`;
  }
  if (method === 'Runtime.exceptionThrown') {
    const d = params.exceptionDetails ?? {};
    return `[exception] ${d.exception?.description ?? d.text ?? 'unknown'}`;
  }
  if (method === 'Log.entryAdded') return `[${params.entry.level}] ${params.entry.text}`;
  return null;
}

// [rc4l] The result of a Runtime.evaluate as one line: its value as JSON, or what it threw.
export function formatEval(result) {
  const d = result?.exceptionDetails;
  if (d) return `[eval] threw ${d.exception?.description ?? d.text}`;
  return `[eval] ${JSON.stringify(result?.result?.value ?? null)}`;
}

// [rc4l] The keys a page may ask smoke to press, with the codes Chrome's input events want.
export const KEYS = { Tab: { code: 'Tab', keyCode: 9 }, Escape: { code: 'Escape', keyCode: 27 }, Enter: { code: 'Enter', keyCode: 13 } };

// [rc4l] Input a page asks for with a console line, since only input from outside it counts as a person's: "check: click" clicks its middle, "check: press KEY" presses one of KEYS, "check: resize WxH" makes the window that size; null for any other line.
export function inputAsked(line) {
  const m = /^\[log\] check: (click|press (\S+)|resize (\d+)x(\d+))$/.exec(line);
  if (!m) return null;
  if (m[3]) return { resize: [Number(m[3]), Number(m[4])] };
  if (!m[2]) return { click: true };
  return KEYS[m[2]] ? { key: m[2], ...KEYS[m[2]] } : null;
}

// [rc4l] Until a download has landed in dir and none is still being written (Chrome's .crdownload), or ten seconds, since closing Chrome mid-download leaves only the partial file.
export async function downloadsSettled(dir, { timeoutMs = 10000, pollMs = 100 } = {}) {
  for (const deadline = Date.now() + timeoutMs; Date.now() < deadline; await new Promise(r => setTimeout(r, pollMs))) {
    const files = fs.readdirSync(dir);
    if (files.length && !files.some(f => f.endsWith('.crdownload'))) return true;
  }
  return false;
}

async function waitForJson(url, deadline) {
  for (;;) {
    try { return await (await fetch(url)).json(); }
    catch (e) { if (Date.now() > deadline) throw e; await new Promise(r => setTimeout(r, 100)); }
  }
}

// [rc4l] Chrome's own log lines about its GPU process and devices, the last `count` of them, the page's console echoed there left out: why a page lost its device, which the page itself is never told.
export function gpuLogLines(text, count = 15) {
  return String(text).split(/\r?\n/)
    .filter((l) => !/:CONSOLE[(:]/.test(l) &&/gpu|dawn|webgpu|swiftshader|vulkan|crash|lost|killed|terminat|exited|out of memory/i.test(l))
    .slice(-count);
}

// [rc4l] `init` runs in the page before its own scripts, the page may ask for input (inputAsked), its downloads land in `downloadDir`, and Chrome's own log goes to `logFile`; the browser's version comes back too.
// [rc4l] Removes a scratch folder, and says so rather than throwing when Chrome's helper processes still hold it, since a leftover temp folder is no reason to fail a run; false when it stayed.
export function removeQuietly(dir, rm = fs.rmSync, warn = console.warn) {
  try {
    rm(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
    return true;
  } catch (e) {
    warn(`left ${dir} behind: ${e.code || e.message}`);
    return false;
  }
}

export async function smoke({ url, until, seconds = 30, shot, chrome, headless = true, software = false, autoplay = false, port = 9333, onLine = console.log, evaluate, after = 10, profile, init, downloadDir, logFile }) {
  const exe = chrome ?? CHROME_CANDIDATES.find(p => fs.existsSync(p));
  if (!exe) throw new Error('no Chrome found');
  // [rc4l] A named profile keeps the page's storage between runs, for checking what survives a reload; otherwise a fresh one each time.
  const dir = profile ?? fs.mkdtempSync(path.join(os.tmpdir(), 'fua-smoke-'));
  const log = logFile ? fs.openSync(logFile, 'w') : 'ignore';
  const proc = spawn(exe, [...(logFile ? ['--enable-logging=stderr', '--v=0'] : []), ...chromeArgs(port, dir, { headless, software, autoplay })], { stdio: ['ignore', 'ignore', log] });
  if (logFile) fs.closeSync(log);
  const lines = [];
  try {
    const deadline = Date.now() + seconds * 1000;
    const targets = await waitForJson(`http://127.0.0.1:${port}/json/list`, deadline);
    const version = (await waitForJson(`http://127.0.0.1:${port}/json/version`, deadline)).Browser;
    const page = targets.find(t => t.type === 'page');
    const ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
    let id = 0;
    const pending = new Map();
    let matched = null;
    let wake = () => {};
    const take = line => {
      lines.push(line);
      onLine(line);
      if (until && new RegExp(until).test(line)) { matched = line; wake(); }
      const asked = inputAsked(line);
      if (asked) act(asked);
    };
    ws.onmessage = ev => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); return; }
      const line = formatConsole(msg.method, msg.params ?? {});
      if (line !== null) take(line);
    };
    const send = (method, params = {}) => new Promise(res => { const n = ++id; pending.set(n, res); ws.send(JSON.stringify({ id: n, method, params })); });
    const act = async (asked) => {
      if (asked.resize) {
        await send('Emulation.setDeviceMetricsOverride', { width: asked.resize[0], height: asked.resize[1], deviceScaleFactor: 1, mobile: false });
        return;
      }
      if (asked.click) {
        for (const type of ['mousePressed', 'mouseReleased']) await send('Input.dispatchMouseEvent', { type, x: 640, y: 360, button: 'left', clickCount: 1 });
        return;
      }
      for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key: asked.key, code: asked.code, windowsVirtualKeyCode: asked.keyCode });
    };
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    if (init) await send('Page.addScriptToEvaluateOnNewDocument', { source: init });
    if (downloadDir) await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: downloadDir });
    await send('Page.navigate', { url });
    // [rc4l] JavaScript run in the page `after` seconds in, such as Module.fuaCommand('map MAP02'), its result printed.
    if (evaluate) {
      setTimeout(async () => {
        const r = await send('Runtime.evaluate', { expression: evaluate, awaitPromise: true, returnByValue: true });
        take(formatEval(r.result));
      }, Number(after) * 1000).unref();
    }
    await new Promise(res => { wake = res; setTimeout(res, Math.max(0, deadline - Date.now())); });
    if (downloadDir) await downloadsSettled(downloadDir);
    if (shot) {
      const r = await send('Page.captureScreenshot', { format: 'png' });
      if (r.result?.data) fs.writeFileSync(shot, Buffer.from(r.result.data, 'base64'));
    }
    ws.close();
    return { matched, lines, version };
  } finally {
    // [rc4l] Chrome is gone before this returns, so the next run can use the same profile and the last can remove it; its helper processes can take a moment longer, hence the retries.
    const exited = new Promise(res => { proc.once('exit', res); setTimeout(res, 5000).unref(); });
    proc.kill();
    await exited;
    if (!profile) removeQuietly(dir);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const opt = { url: process.argv[2] };
  for (let i = 3; i < process.argv.length; i += 2) {
    const k = process.argv[i].replace(/^--/, '');
    opt[k] = process.argv[i + 1];
  }
  if (opt.seconds) opt.seconds = Number(opt.seconds);
  if (opt.headless) opt.headless = opt.headless !== 'false';
  if (opt.software) opt.software = opt.software !== 'false';
  const { matched } = await smoke(opt);
  if (opt.until && !matched) { console.error(`no line matched /${opt.until}/`); process.exit(1); }
  process.exit(0);
}
