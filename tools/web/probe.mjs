#!/usr/bin/env node
// [rc4l] Plays a staged page through console steps and reports what it did (what the steps printed, the sounds it started and any started again, the screen textures it held, its WebGPU errors), or soaks it (--soak) for what memory kept growing.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from './serve.mjs';
import { smoke } from './smoke.mjs';
import { smokeFirefox } from './firefox.mjs';
import { WATCH_SOUNDS, WATCH_TEXTURES, WATCH_FRAMES, WATCH_LEVEL, HEAR_CONSOLE, inPage, pause, repeatedSounds, soakInPage, framePacing } from './page.mjs';

// [rc4l] What a run's look steps print, each about the time since the run began or the last look of its kind: "frames" the browser's frame pacing, "level" what the audio output carried.
const LOOKS = {
  frames: 'console.log(framePacing(window.fuaFrames.slice(framesFrom))); framesFrom = window.fuaFrames.length;',
  level: "console.log('level: ' + window.fuaLevel.mean().toFixed(4) + ' RMS'); window.fuaLevel.reset();",
};

// [rc4l] A run as steps: "god; wait 1000; +attack; wait 4000; frames" is console lines with waits in milliseconds between them and looks at what the page did.
export function parseRun(text) {
  return String(text || '').split(';').map((s) => s.trim()).filter(Boolean)
    .map((s) => (/^wait \d+$/.test(s) ? { wait: Number(s.slice(5)) } : LOOKS[s] ? { look: s } : { command: s }));
}

// [rc4l] One step as page code.
function stepCode(s) {
  if (s.wait !== undefined) return pause(s.wait);
  return s.look ? LOOKS[s.look] : `Module.fuaCommand(${JSON.stringify(s.command)});`;
}

// [rc4l] The page code a run is: each command through Module.fuaCommand, each wait a pause, each look printed, then what the steps printed and what the watches saw since the first step.
export function runScript(steps) {
  const body = steps.map(stepCode).join('\n');
  return inPage(`const from = window.fuaSounds.length, heldBefore = window.fuaHeld.size;
    let framesFrom = window.fuaFrames.length;
    window.fuaLevel.reset();
    ${framePacing.toString()}
    ${HEAR_CONSOLE}
    ${body}
    unhear();
    return { said, sounds: window.fuaSounds.slice(from), heldBefore, heldAfter: window.fuaHeld.size };`);
}

// [rc4l] The map a page's query starts on, which a soak reloads.
export function queryMap(query) {
  return new URLSearchParams(query).get('map') || 'MAP01';
}

// [rc4l] A soak's verdict as the engine reported it, or why there was none.
export function soakLines(verdict) {
  return String(verdict.report || 'the soak gave no verdict').trimEnd().split('\n');
}

// [rc4l] A stream's pieces (the music's, 71 ms each) are shorter than this, and are counted rather than listed.
const PIECE_S = 0.1;

// [rc4l] The report: what the steps printed, each sound started, any started again, the stream's pieces counted, the screen textures held before and after, and the first WebGPU errors.
export function report({ said = [], sounds, heldBefore, heldAfter }, lines) {
  const played = sounds.filter((s) => s.len >= PIECE_S);
  const out = said.map((l) => `> ${l}`);
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
  const dir = path.resolve(args[0] ?? 'dist-web');
  const server = createServer(dir).listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  const { matched, lines } = await (browser === 'firefox' ? smokeFirefox : smoke)({ url: `http://127.0.0.1:${server.address().port}/?${query}`, seconds, software, autoplay,
    focus: true, init: `${WATCH_SOUNDS}\n${WATCH_TEXTURES}\n${WATCH_FRAMES}\n${WATCH_LEVEL}`, evaluate: soak ? soakInPage(soak, cycles, settle, queryMap(query)) : runScript(parseRun(run)),
    until: '^\\[eval\\]', after: 1, onLine: () => {} });
  server.close();
  if (!matched || matched.startsWith('[eval] threw')) { console.error(matched || 'the page never finished the run'); process.exit(1); }
  const result = JSON.parse(matched.slice('[eval] '.length));
  for (const l of soak ? soakLines(result) : report(result, lines)) console.log(l);
  process.exit(soak && !result.pass ? 1 : 0);
}
