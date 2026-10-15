#!/usr/bin/env node
// [rc4l] Plays a staged page through console steps and reports what it did: the sounds it started (and any started again), the screen textures it held, and the WebGPU errors it raised.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from './serve.mjs';
import { smoke } from './smoke.mjs';
import { WATCH_SOUNDS, WATCH_TEXTURES, inPage, pause, repeatedSounds } from './page.mjs';

// [rc4l] A run as steps: "god; give all; wait 1000; +attack" is console lines with waits in milliseconds between them.
export function parseRun(text) {
  return String(text || '').split(';').map((s) => s.trim()).filter(Boolean)
    .map((s) => (/^wait \d+$/.test(s) ? { wait: Number(s.slice(5)) } : { command: s }));
}

// [rc4l] The page code a run is: each command through Module.fuaCommand, each wait a pause, then what the watches saw since the first step.
export function runScript(steps) {
  const body = steps.map((s) => (s.wait !== undefined ? pause(s.wait) : `Module.fuaCommand(${JSON.stringify(s.command)});`)).join('\n');
  return inPage(`const from = window.fuaSounds.length, heldBefore = window.fuaHeld.size;
    ${body}
    return { sounds: window.fuaSounds.slice(from), heldBefore, heldAfter: window.fuaHeld.size };`);
}

// [rc4l] A stream's pieces (the music's, 71 ms each) are shorter than this, and are counted rather than listed.
const PIECE_S = 0.1;

// [rc4l] The report: each sound started, any started again, the stream's pieces counted, the screen textures held before and after, and the first WebGPU errors.
export function report({ sounds, heldBefore, heldAfter }, lines) {
  const played = sounds.filter((s) => s.len >= PIECE_S);
  const out = played.map((s) => `sound #${s.id} (${s.len.toFixed(3)} s) at ${s.t.toFixed(3)}`);
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
  const autoplay = on('--autoplay'), software = on('--software');
  const dir = path.resolve(args[0] ?? 'dist-web');
  const server = createServer(dir).listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  const { matched, lines } = await smoke({ url: `http://127.0.0.1:${server.address().port}/?${query}`, seconds, software, autoplay,
    init: `${WATCH_SOUNDS}\n${WATCH_TEXTURES}`, evaluate: runScript(parseRun(run)), until: '^\\[eval\\]', after: 1, onLine: () => {} });
  server.close();
  if (!matched || matched.startsWith('[eval] threw')) { console.error(matched || 'the page never finished the run'); process.exit(1); }
  for (const l of report(JSON.parse(matched.slice('[eval] '.length)), lines)) console.log(l);
  process.exit(0);
}
