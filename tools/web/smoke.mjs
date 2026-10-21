#!/usr/bin/env node
// [rc4l] Loads a page in headless Chrome with WebGPU, prints its console, and saves a screenshot once a line matches or time runs out.
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { resizeAsked } from '../fuactl/src/telemetry.mjs';
import { heapSummary } from './heap.mjs';

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
    // [rc4l] window.gc, so a soak measures what the page keeps rather than what its collector has not reached yet.
    '--js-flags=--expose-gc',
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
export const KEYS = { Tab: { code: 'Tab', keyCode: 9 }, Escape: { code: 'Escape', keyCode: 27 }, Enter: { code: 'Enter', keyCode: 13 }, ArrowUp: { code: 'ArrowUp', keyCode: 38 } };

// [rc4l] Input a page asks for with a console line, since only input from outside it counts as a person's: "check: click" clicks its middle and "check: click ID" the element with that id, "check: press KEY" presses one of KEYS, "check: hold KEY MS" holds one that long, "check: down KEY" and "check: up KEY" hold one until the page lets it go, "check: resize WxH" makes the window that size (fuactl's resizeAsked, which a desktop soak takes too); null for any other line.
export function inputAsked(line) {
  if (!line.startsWith('[log] ')) return null;
  const size = resizeAsked(line.slice(6));
  if (size) return { resize: size };
  const m = /^\[log\] check: (?:click(?: (?<target>[\w-]+))?|press (?<press>\S+)|hold (?<hold>\S+) (?<ms>\d+)|(?<phase>down|up) (?<edge>\S+)|heap (?<heap>\w+)|gc (?<gc>\d+))$/.exec(line);
  if (!m) return null;
  const { target, press, hold, ms, phase, edge, heap, gc } = m.groups;
  if (heap) return { heap };
  if (gc) return { gc: Number(gc) };
  if (!press && !hold && !edge) return target ? { click: true, target } : { click: true };
  const key = press || hold || edge;
  if (!KEYS[key]) return null;
  if (edge) return { key, ...KEYS[key], phase };
  return hold ? { key, ...KEYS[key], holdMs: Number(ms) } : { key, ...KEYS[key] };
}

const pause = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] What attempt returns once it is not undefined, tried again after each wait until deadline, past which its last error, or a timeout, is thrown.
export async function retry(attempt, deadline, wait = () => pause(100)) {
  for (;;) {
    let error = null;
    try {
      const value = await attempt();
      if (value !== undefined) return value;
    } catch (e) { error = e; }
    if (Date.now() > deadline) throw error ?? new Error('timed out');
    await wait();
  }
}

// [rc4l] Until a download has landed in dir and none is still being written (Chrome's .crdownload), or ten seconds, since closing Chrome mid-download leaves only the partial file.
export function downloadsSettled(dir, { timeoutMs = 10000, pollMs = 100 } = {}) {
  const settled = () => { const files = fs.readdirSync(dir); return files.length && !files.some(f => f.endsWith('.crdownload')) ? true : undefined; };
  return retry(settled, Date.now() + timeoutMs, () => pause(pollMs)).catch(() => false);
}

const waitForJson = (url, deadline) => retry(async () => (await fetch(url)).json(), deadline);

// [rc4l] The debugging port a Chrome started with --remote-debugging-port=0 chose, from the DevToolsActivePort file it writes in its profile, so a run reaches its own Chrome and never one a stale run left on a fixed port.
export function devToolsPort(dir, deadline, { read = (f) => fs.readFileSync(f, 'utf8'), wait } = {}) {
  const port = () => { const p = Number(read(path.join(dir, 'DevToolsActivePort')).split(/\r?\n/)[0]); return p > 0 ? p : undefined; };
  return retry(port, deadline, wait).catch(() => { throw new Error(`Chrome wrote no DevToolsActivePort in ${dir}`); });
}

// [rc4l] Chrome's own log lines about its GPU process and devices, the last `count` of them, the page's console echoed there left out: why a page lost its device, which the page itself is never told.
export function gpuLogLines(text, count = 15) {
  return String(text).split(/\r?\n/)
    .filter((l) => !/:CONSOLE[(:]/.test(l) &&/gpu|dawn|webgpu|swiftshader|vulkan|crash|lost|killed|terminat|exited|out of memory/i.test(l))
    .slice(-count);
}

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

// [rc4l] `init` runs in the page before its own scripts, the page may ask for input (inputAsked), its downloads land in `downloadDir`, and Chrome's own log goes to `logFile`; the browser's version comes back too.
export async function smoke({ url, until, seconds = 30, shot, chrome, headless = true, software = false, throttle = 0, autoplay = false, focus = false, onLine = console.log, evaluate, after = 10, profile, init, downloadDir, logFile }) {
  const exe = chrome ?? CHROME_CANDIDATES.find(p => fs.existsSync(p));
  if (!exe) throw new Error('no Chrome found');
  // [rc4l] A named profile keeps the page's storage between runs, for checking what survives a reload; otherwise a fresh one each time.
  const dir = profile ?? fs.mkdtempSync(path.join(os.tmpdir(), 'fua-smoke-'));
  // [rc4l] The last run's port file goes first, so the port read back is this Chrome's.
  fs.rmSync(path.join(dir, 'DevToolsActivePort'), { force: true });
  const log = logFile ? fs.openSync(logFile, 'w') : 'ignore';
  const proc = spawn(exe, [...(logFile ? ['--enable-logging=stderr', '--v=0'] : []), ...chromeArgs(0, dir, { headless, software, autoplay })], { stdio: ['ignore', 'ignore', log] });
  if (logFile) fs.closeSync(log);
  const lines = [];
  try {
    const deadline = Date.now() + seconds * 1000;
    const port = await devToolsPort(dir, deadline);
    const targets = await waitForJson(`http://127.0.0.1:${port}/json/list`, deadline);
    const version = (await waitForJson(`http://127.0.0.1:${port}/json/version`, deadline)).Browser;
    const page = targets.find(t => t.type === 'page');
    const ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
    let id = 0;
    const pending = new Map();
    let matched = null;
    let wake = () => {};
    // [rc4l] Heap snapshots the page asked for (check: heap LABEL), each summed by heap.mjs, and the pieces of the one being taken.
    const heaps = {};
    let heapChunks = null;
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
      if (msg.method === 'HeapProfiler.addHeapSnapshotChunk') { if (heapChunks) heapChunks.push(msg.params.chunk); return; }
      const line = formatConsole(msg.method, msg.params ?? {});
      if (line !== null) take(line);
    };
    const send = (method, params = {}) => new Promise(res => { const n = ++id; pending.set(n, res); ws.send(JSON.stringify({ id: n, method, params })); });
    const act = async (asked) => {
      // [rc4l] A snapshot after a collection, summed, then the page told it may go on.
      if (asked.heap) {
        await send('HeapProfiler.enable');
        await send('HeapProfiler.collectGarbage');
        heapChunks = [];
        await send('HeapProfiler.takeHeapSnapshot', { reportProgress: false });
        heaps[asked.heap] = heapSummary(JSON.parse(heapChunks.join('')));
        heapChunks = null;
        await send('Runtime.evaluate', { expression: `window.fuaHeapTaken = ${JSON.stringify(asked.heap)}` });
        return;
      }
      // [rc4l] A collection as a heap snapshot makes one, which reaches garbage the page's own gc() leaves, then the page told it is done.
      if (asked.gc) {
        await send('HeapProfiler.enable');
        await send('HeapProfiler.collectGarbage');
        await send('Runtime.evaluate', { expression: `window.fuaGcTaken = ${asked.gc}` });
        return;
      }
      if (asked.resize) {
        await send('Emulation.setDeviceMetricsOverride', { width: asked.resize[0], height: asked.resize[1], deviceScaleFactor: 1, mobile: false });
        return;
      }
      if (asked.click) {
        let at = { x: 640, y: 360 };
        if (asked.target) {
          const r = await send('Runtime.evaluate', { expression: `(() => { const b = document.getElementById(${JSON.stringify(asked.target)}).getBoundingClientRect(); return { x: b.x + b.width / 2, y: b.y + b.height / 2 }; })()`, returnByValue: true });
          at = r.result?.result?.value ?? at;
        }
        for (const type of ['mousePressed', 'mouseReleased']) await send('Input.dispatchMouseEvent', { type, ...at, button: 'left', clickCount: 1 });
        return;
      }
      const key = (type) => send('Input.dispatchKeyEvent', { type, key: asked.key, code: asked.code, windowsVirtualKeyCode: asked.keyCode });
      if (asked.phase) { await key(asked.phase === 'down' ? 'keyDown' : 'keyUp'); return; }
      await key('keyDown');
      if (asked.holdMs) await pause(asked.holdMs);
      await key('keyUp');
    };
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    // [rc4l] A focused page, as a player's is: headless Chrome's fullscreen otherwise resizes the whole viewport, which hid a canvas resized without a resize event.
    if (focus) await send('Emulation.setFocusEmulationEnabled', { enabled: true });
    // [rc4l] The page's thread slowed `throttle` times, as a slow runner's is.
    if (throttle > 1) await send('Emulation.setCPUThrottlingRate', { rate: throttle });
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
    return { matched, lines, version, heaps };
  } finally {
    // [rc4l] Chrome is gone before this returns, so the next run can use the same profile and the last can remove it; its helper processes can take a moment longer, hence the retries.
    let gone = false;
    const exited = new Promise(res => { proc.once('exit', () => { gone = true; res(); }); setTimeout(res, 5000).unref(); });
    proc.kill();
    await exited;
    // [rc4l] A Chrome that outlives its kill took every later run on its port, so on Windows its whole tree goes.
    if (!gone && process.platform === 'win32') spawnSync('taskkill', ['/pid', String(proc.pid), '/T', '/F'], { stdio: 'ignore' });
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
