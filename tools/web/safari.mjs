#!/usr/bin/env node
// [rc4l] The browser build in real Safari through safaridriver, which needs Develop > Allow Remote Automation: whether it starts, draws without a device error, hears a click and loads a second map.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveLocally } from './serve.mjs';

const DRIVER_PORT = 4455;

// [rc4l] Page code run before the page's own: what it logs and its audio contexts, which Safari gives a driver no other way to read.
export const WATCH = `<script>window.fuaSaid = []; window.fuaCtxs = []; for (const k of ['log', 'warn', 'error']) { const o = console[k]; console[k] = (...a) => { fuaSaid.push(a.join(' ')); return o.apply(console, a); }; } const A = window.AudioContext; window.AudioContext = class extends A { constructor(...a) { super(...a); fuaCtxs.push(this); } };</script>`;

// [rc4l] A page with the watch at the top of its head.
export function withWatch(html) {
  return html.replace('<head>', `<head>\n${WATCH}`);
}

// [rc4l] What went wrong in a run's report, each a line; none when Safari ran the game.
export function safariFaults({ up, maps, audio, errors, crashed }) {
  const faults = [];
  if (!up) faults.push('the engine never started');
  for (const [map, said] of Object.entries(maps || {})) if (!said.startsWith(`Level ${map}`)) faults.push(`${map} did not load: ${said}`);
  if (up && !(audio || []).includes('running')) faults.push(`the sound did not start after a click (${(audio || []).join(', ')})`);
  for (const line of errors || []) faults.push(`logged: ${line}`);
  if (crashed) faults.push('the page showed its error panel');
  return faults;
}

// [rc4l] A copy of dist of links with the watch in its page, so the build itself stays as it was staged.
function watchedCopy(dist) {
  const copy = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-safari-'));
  for (const f of fs.readdirSync(dist)) if (f !== 'index.html') fs.symlinkSync(path.resolve(dist, f), path.join(copy, f));
  fs.writeFileSync(path.join(copy, 'index.html'), withWatch(fs.readFileSync(path.join(dist, 'index.html'), 'utf8')));
  return copy;
}

async function run(dist, { map = 'MAP01', next = 'MAP02', shot }) {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const wd = async (method, p, body) => {
    const r = await fetch(`http://localhost:${DRIVER_PORT}${p}`, { method, headers: { 'content-type': 'application/json' }, body: body ? JSON.stringify(body) : undefined });
    const j = await r.json();
    if (j.value && j.value.error) throw new Error(`${p}: ${j.value.message}`);
    return j.value;
  };
  const copy = watchedCopy(dist);
  const { server, origin } = await serveLocally(copy);
  const driver = spawn('/usr/bin/safaridriver', ['-p', String(DRIVER_PORT)], { stdio: 'ignore' });
  await sleep(1500);
  let sid;
  try {
    sid = (await wd('POST', '/session', { capabilities: { alwaysMatch: { browserName: 'safari' } } })).sessionId;
    const ex = (script) => wd('POST', `/session/${sid}/execute/sync`, { script, args: [] });
    await wd('POST', `/session/${sid}/url`, { url: `${origin.replace('127.0.0.1', 'localhost')}/?map=${map}` });
    let up = false;
    for (let i = 0; i < 180 && !(up = await ex('return !!(window.Module && Module.fuaCommand)')); i++) await sleep(1000);
    if (!up) return { up };
    await sleep(6000);
    const stats = async () => {
      const from = await ex('return fuaSaid.length');
      await ex("Module.fuaCommand('printstats')");
      await sleep(800);
      return (await ex(`return fuaSaid.slice(${from})`)).join(' ');
    };
    const maps = { [map]: await stats() };
    const canvas = await wd('POST', `/session/${sid}/element`, { using: 'css selector', value: '#canvas' });
    await wd('POST', `/session/${sid}/actions`, { actions: [{ type: 'pointer', id: 'm', parameters: { pointerType: 'mouse' }, actions: [{ type: 'pointerMove', origin: { 'element-6066-11e4-a52e-4f735466cecf': Object.values(canvas)[0] }, x: 0, y: 0 }, { type: 'pointerDown', button: 0 }, { type: 'pause', duration: 80 }, { type: 'pointerUp', button: 0 }] }] });
    await sleep(2500);
    const audio = await ex('return fuaCtxs.map((c) => c.state)');
    await ex(`Module.fuaCommand('map ${next}')`);
    await sleep(5000);
    maps[next] = await stats();
    if (shot) fs.writeFileSync(shot, Buffer.from(await wd('GET', `/session/${sid}/screenshot`), 'base64'));
    const said = await ex('return fuaSaid');
    return { up, maps, audio, errors: said.filter((l) => /\berror\b|fatal|abort|exception/i.test(l)).slice(0, 8), crashed: await ex('return !document.getElementById("error").hidden') };
  } finally {
    if (sid) await wd('DELETE', `/session/${sid}`).catch(() => {});
    driver.kill();
    server.close();
    fs.rmSync(copy, { recursive: true, force: true });
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [dist = 'dist-web', ...rest] = process.argv.slice(2);
  const opt = {};
  for (let i = 0; i < rest.length; i += 2) opt[rest[i].replace(/^--/, '')] = rest[i + 1];
  const report = await run(dist, opt);
  console.log(JSON.stringify(report, null, 1));
  const faults = safariFaults(report);
  for (const f of faults) console.error(`FAIL ${f}`);
  console.log(faults.length ? `${faults.length} fault(s) in Safari` : 'Safari runs the build');
  process.exit(faults.length ? 1 : 0);
}
