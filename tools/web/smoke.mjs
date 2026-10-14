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

// [rc4l] `software` puts WebGPU on SwiftShader, Chrome's CPU renderer, for a machine with no GPU such as a CI runner.
export function chromeArgs(port, profileDir, { headless = true, software = false } = {}) {
  return [
    ...(headless ? ['--headless=new'] : []),
    ...(software ? ['--use-webgpu-adapter=swiftshader', '--enable-unsafe-swiftshader'] : []),
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

// [rc4l] Input a page asks for with a console line, since only input from outside it counts as a person's: "check: click" clicks its middle, "check: press KEY" presses one of KEYS; null for any other line.
export function inputAsked(line) {
  const m = /^\[log\] check: (click|press (\S+))$/.exec(line);
  if (!m) return null;
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

// [rc4l] `init` runs in the page before its own scripts, the page may ask for input (inputAsked), and its downloads land in `downloadDir`.
export async function smoke({ url, until, seconds = 30, shot, chrome, headless = true, software = false, port = 9333, onLine = console.log, evaluate, after = 10, profile, init, downloadDir }) {
  const exe = chrome ?? CHROME_CANDIDATES.find(p => fs.existsSync(p));
  if (!exe) throw new Error('no Chrome found');
  // [rc4l] A named profile keeps the page's storage between runs, for checking what survives a reload; otherwise a fresh one each time.
  const dir = profile ?? fs.mkdtempSync(path.join(os.tmpdir(), 'fua-smoke-'));
  const proc = spawn(exe, chromeArgs(port, dir, { headless, software }), { stdio: 'ignore' });
  const lines = [];
  try {
    const deadline = Date.now() + seconds * 1000;
    const targets = await waitForJson(`http://127.0.0.1:${port}/json/list`, deadline);
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
    return { matched, lines };
  } finally {
    // [rc4l] Chrome is gone before this returns, so the next run can use the same profile and the last can remove it; its helper processes can take a moment longer, hence the retries.
    const exited = new Promise(res => { proc.once('exit', res); setTimeout(res, 5000).unref(); });
    proc.kill();
    await exited;
    if (!profile) fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
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
