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

export function chromeArgs(port, profileDir, { headless = true } = {}) {
  return [
    ...(headless ? ['--headless=new'] : []),
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

async function waitForJson(url, deadline) {
  for (;;) {
    try { return await (await fetch(url)).json(); }
    catch (e) { if (Date.now() > deadline) throw e; await new Promise(r => setTimeout(r, 100)); }
  }
}

export async function smoke({ url, until, seconds = 30, shot, chrome, headless = true, port = 9333, onLine = console.log }) {
  const exe = chrome ?? CHROME_CANDIDATES.find(p => fs.existsSync(p));
  if (!exe) throw new Error('no Chrome found');
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-smoke-'));
  const proc = spawn(exe, chromeArgs(port, profile, { headless }), { stdio: 'ignore' });
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
    ws.onmessage = ev => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); return; }
      const line = formatConsole(msg.method, msg.params ?? {});
      if (line === null) return;
      lines.push(line);
      onLine(line);
      if (until && new RegExp(until).test(line)) { matched = line; wake(); }
    };
    const send = (method, params = {}) => new Promise(res => { const n = ++id; pending.set(n, res); ws.send(JSON.stringify({ id: n, method, params })); });
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    await send('Page.navigate', { url });
    await new Promise(res => { wake = res; setTimeout(res, Math.max(0, deadline - Date.now())); });
    if (shot) {
      const r = await send('Page.captureScreenshot', { format: 'png' });
      if (r.result?.data) fs.writeFileSync(shot, Buffer.from(r.result.data, 'base64'));
    }
    ws.close();
    return { matched, lines };
  } finally {
    proc.kill();
    setTimeout(() => fs.rmSync(profile, { recursive: true, force: true }), 1000).unref();
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
  const { matched } = await smoke(opt);
  if (opt.until && !matched) { console.error(`no line matched /${opt.until}/`); process.exit(1); }
  process.exit(0);
}
