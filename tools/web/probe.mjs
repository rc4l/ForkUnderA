#!/usr/bin/env node
// [rc4l] Plays a staged page through console steps and reports what it did (what the steps printed, the sounds it started and any started again, the screen textures it held, its WebGPU errors), or soaks it (--soak) for what memory kept growing.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveLocally } from './serve.mjs';
import { smokeIn } from './firefox.mjs';
import { WATCH_SOUNDS, WATCH_TEXTURES, WATCH_FRAMES, WATCH_LEVEL, HEAR_CONSOLE, GRAB_FRAME, PIECE_S, inPage, pause, repeatedSounds, serveData, soakInPage, swapWgsl, framePacing } from './page.mjs';
import { decodePNGBytes, boxDiff, boxDiffMax } from '../fuactl/src/pngcore.mjs';

// [rc4l] What a run's look steps print, each about the time since the run began or the last look of its kind: "frames" the browser's frame pacing, "level" what the audio output carried.
const LOOKS = {
  frames: 'console.log(framePacing(window.fuaFrames.slice(framesFrom))); framesFrom = window.fuaFrames.length;',
  level: "console.log('level: ' + window.fuaLevel.mean().toFixed(4) + ' RMS'); window.fuaLevel.reset();",
};

// [rc4l] A run as steps: "god; wait 1000; +attack; wait 4000; frames; ab fua_dg_clusters 1 0" is console lines, waits in milliseconds, looks at what the page did and ab steps that frame a cvar at two values.
export function parseRun(text) {
  return String(text || '').split(';').map((s) => s.trim()).filter(Boolean).map((s) => {
    if (/^wait \d+$/.test(s)) return { wait: Number(s.slice(5)) };
    const ab = /^ab (\S+) (\S+) (\S+)$/.exec(s);
    if (ab) return { ab: { cvar: ab[1], a: ab[2], b: ab[3] } };
    return LOOKS[s] ? { look: s } : { command: s };
  });
}

// [rc4l] An ab step as page code: a frame with the cvar at a and one at b, kept in abs, and the cvar set back to what it was.
function abCode({ cvar, a, b }) {
  return `{
      const was = (/" is "([^"]*)"/.exec((await ask(${JSON.stringify(cvar)})).join(' ')) || [])[1];
      Module.fuaCommand(${JSON.stringify(`${cvar} ${a}`)}); ${pause(300)} const first = await grabFrame();
      Module.fuaCommand(${JSON.stringify(`${cvar} ${b}`)}); ${pause(300)} const second = await grabFrame();
      if (was !== undefined) Module.fuaCommand(${JSON.stringify(cvar)} + ' "' + was + '"');
      abs.push({ cvar: ${JSON.stringify(cvar)}, a: ${JSON.stringify(a)}, b: ${JSON.stringify(b)}, frames: [first, second] });
    }`;
}

// [rc4l] One step as page code.
function stepCode(s) {
  if (s.wait !== undefined) return pause(s.wait);
  if (s.ab) return abCode(s.ab);
  return s.look ? LOOKS[s.look] : `Module.fuaCommand(${JSON.stringify(s.command)});`;
}

// [rc4l] The page code a run is: each command through Module.fuaCommand, each wait a pause, each look printed, each ab's frames kept, then what the steps printed and what the watches saw since the first step.
export function runScript(steps) {
  const body = steps.map(stepCode).join('\n');
  return inPage(`const from = window.fuaSounds.length, heldBefore = window.fuaHeld.size;
    let framesFrom = window.fuaFrames.length;
    window.fuaLevel.reset();
    ${framePacing.toString()}
    ${HEAR_CONSOLE}
    ${GRAB_FRAME}
    const abs = [];
    ${body}
    unhear();
    return { said, sounds: window.fuaSounds.slice(from), heldBefore, heldAfter: window.fuaHeld.size, abs };`);
}

// [rc4l] Each ab step's two frames as files to write, named by its order, cvar and value.
export function abShots(abs = []) {
  return abs.flatMap(({ cvar, a, b, frames }, i) => [a, b].map((v, k) => ({ name: `ab${i + 1}-${cvar}-${v}.png`, bytes: Buffer.from(frames[k], 'base64') })));
}

// [rc4l] An ab step's verdict: how far its two frames sit apart over the whole frame, in 16-pixel block means as the scenarios judge, and the largest block.
export function abLine({ cvar, a, b, frames }) {
  const [first, second] = frames.map((f) => decodePNGBytes(Buffer.from(f, 'base64')));
  return `ab ${cvar} ${a} vs ${b}: mean|d| ${boxDiff(first, second, [0, 0, 1, 1]).toFixed(3)}, largest block ${boxDiffMax(first, second, [0, 0, 1, 1]).toFixed(2)}`;
}

// [rc4l] A --wgsl swap as its find and replace, split at the first =>.
export function parseSwap(text) {
  const at = String(text).indexOf('=>');
  if (at <= 0) throw new Error(`--wgsl takes FIND=>REPLACE, not ${JSON.stringify(text)}`);
  return { find: text.slice(0, at), replace: text.slice(at + 2) };
}

// [rc4l] Page code to run before the page's own: the watches, each --wad served as if staged, and a --wgsl swap.
export function initFor({ wads = [], swap = null } = {}) {
  return [WATCH_SOUNDS, WATCH_TEXTURES, WATCH_FRAMES, WATCH_LEVEL, ...wads.map((w) => serveData(w.name, w.bytes)),
    ...(swap ? [swapWgsl(swap.find, swap.replace)] : [])].join('\n');
}

// [rc4l] The map a page's query starts on, which a soak reloads.
export function queryMap(query) {
  return new URLSearchParams(query).get('map') || 'MAP01';
}

// [rc4l] A soak's verdict as the engine reported it, or why there was none.
export function soakLines(verdict) {
  return String(verdict.report || 'the soak gave no verdict').trimEnd().split('\n');
}

// [rc4l] The report: what the steps printed, each sound started, any started again, the stream's pieces counted, the screen textures held before and after, and the first WebGPU errors.
export function report({ said = [], sounds, heldBefore, heldAfter, abs = [] }, lines) {
  const played = sounds.filter((s) => s.len >= PIECE_S);
  const out = said.map((l) => `> ${l}`);
  out.push(...abs.map(abLine));
  out.push(...played.map((s) => `sound #${s.id} (${s.len.toFixed(3)} s) at ${s.t.toFixed(3)}`));
  if (played.length < sounds.length) out.push(`${sounds.length - played.length} stream piece(s) under ${PIECE_S} s`);
  for (const r of repeatedSounds(played)) out.push(`AGAIN: sound #${r.id} (${r.len.toFixed(3)} s) started ${r.times.length} times, at ${r.times.map((t) => t.toFixed(3)).join(', ')}`);
  out.push(`screen textures held: ${heldBefore} before, ${heldAfter} after`);
  for (const l of lines.filter((l) => l.includes('WebGPU error:')).slice(0, 5)) out.push(l.replace(/\s+/g, ' ').slice(0, 300));
  return out;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const flag = (name, fallback) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : fallback; };
  const on = (name) => { const i = args.indexOf(name); return i >= 0 && args.splice(i, 1).length > 0; };
  const query = flag('--query', 'map=MAP01'), run = flag('--run', ''), seconds = Number(flag('--seconds', 120));
  const soak = flag('--soak', ''), cycles = Number(flag('--cycles', 6)), settle = Number(flag('--settle', 4000));
  const autoplay = on('--autoplay'), software = on('--software'), browser = flag('--browser', 'chrome');
  const wads = flag('--wad', '').split(',').filter(Boolean).map((w) => ({ name: path.basename(w), bytes: fs.readFileSync(w) }));
  const swap = flag('--wgsl', null), shots = flag('--shots', null);
  const dir = path.resolve(args[0] ?? 'dist-web');
  const { server, origin } = await serveLocally(dir);
  const { matched, lines } = await smokeIn(browser)({ url: `${origin}/?${query}`, seconds, software, autoplay,
    focus: true, init: initFor({ wads, swap: swap ? parseSwap(swap) : null }), evaluate: soak ? soakInPage(soak, cycles, settle, queryMap(query)) : runScript(parseRun(run)),
    until: '^\\[eval\\]', after: 1, onLine: () => {} });
  server.close();
  if (!matched || matched.startsWith('[eval] threw')) { console.error(matched || 'the page never finished the run'); process.exit(1); }
  const result = JSON.parse(matched.slice('[eval] '.length));
  for (const l of soak ? soakLines(result) : report(result, lines)) console.log(l);
  if (shots) {
    fs.mkdirSync(shots, { recursive: true });
    for (const s of abShots(result.abs)) fs.writeFileSync(path.join(shots, s.name), s.bytes);
  }
  process.exit(soak && !result.pass ? 1 : 0);
}
