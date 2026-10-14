#!/usr/bin/env node
// [rc4l] The browser build's checks in headless Chrome, in order on one profile: it boots and draws, and a setting survives quitting, leaving and reloading the page.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from './serve.mjs';
import { smoke } from './smoke.mjs';
import { decodePNG, boxStats } from '../fuactl/src/pngcore.mjs';

// [rc4l] JavaScript run in the page once the engine has started (fua.js sets fuaCommand just before main), its value the check's result.
export function inPage(body) {
  return `(async () => { while (!(window.Module && Module.fuaCommand)) await new Promise((r) => setTimeout(r, 100)); ${body} })()`;
}

const pause = (ms) => `await new Promise((r) => setTimeout(r, ${ms}));`;

// [rc4l] Counts the animation frames the page asks for from here on, which stop when the engine's loop does.
const COUNT_FRAMES = `let frames = 0;
      const raf = window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame = (f) => { frames++; return raf(f); };`;

// [rc4l] The line smoke prints an evaluation's result on.
const EVAL = '^\\[eval\\]';

// [rc4l] Why a screenshot shows no game, or '': the canvas above the page's footer black or one flat colour.
export function blankGame(img) {
  const s = boxStats(img, [0.05, 0.05, 0.95, 0.8]);
  if (s.mean < 10) return `the game is black (mean ${s.mean.toFixed(1)})`;
  if (s.hi - s.lo < 40) return `the game is one flat colour (${s.lo.toFixed(0)}..${s.hi.toFixed(0)})`;
  return '';
}

// [rc4l] Why the sound check failed, or '': the engine's audio contexts held until a click (a page may not play before one), all ran after it, and buffers went on playing (the level's music streams them).
export function soundStarts({ before, after, played }) {
  if (!after.length) return 'the engine made no audio context';
  if (!before.includes('suspended')) return `the sound did not wait for a click (${before.join(', ')})`;
  if (after.some((s) => s !== 'running')) return `the sound stayed ${after.join(', ')} after a click`;
  if (!played) return 'nothing played in the two seconds after the click';
  return '';
}

// [rc4l] Why a save's picture is wrong, or '': the save missing, still the 1x1 placeholder it is written with before the picture comes back, or one flat colour (a read of bytes not yet there).
export function savePicture({ found, width, height, range }) {
  if (!found) return 'the save was not written';
  if (width <= 1 || height <= 1) return `the save kept its placeholder picture (${width}x${height})`;
  if (range < 40) return `the save's picture is one flat colour (range ${range})`;
  return '';
}

export const CHECKS = [
  { name: 'boots and draws MAP01', evaluate: inPage(`${pause(3000)} return 'drawn';`), until: EVAL, shot: true, expectShot: blankGame },
  {
    name: 'the sound waits for a click and plays after one',
    init: `window.fuaAudio = []; window.fuaBuffers = 0; {
      const AC = window.AudioContext; window.AudioContext = class extends AC { constructor(...a) { super(...a); window.fuaAudio.push(this); } };
      const make = BaseAudioContext.prototype.createBufferSource; BaseAudioContext.prototype.createBufferSource = function (...a) { window.fuaBuffers++; return make.apply(this, a); };
    }`,
    evaluate: inPage(`
      const before = window.fuaAudio.map((c) => c.state);
      console.log('check: click');
      for (let i = 0; i < 50 && !window.fuaAudio.every((c) => c.state === 'running'); i++) ${pause(100)}
      const buffers = window.fuaBuffers;
      ${pause(2000)}
      return { before, after: window.fuaAudio.map((c) => c.state), played: window.fuaBuffers - buffers };`),
    until: EVAL,
    expect: soundStarts,
  },
  // [rc4l] Tab is the automap's, so the page keeps the browser from moving the focus off the game with it.
  {
    name: 'game keys stay in the game',
    evaluate: inPage(`
      document.getElementById('canvas').focus();
      console.log('check: press Tab');
      ${pause(500)}
      return document.activeElement ? document.activeElement.id || document.activeElement.tagName : null;`),
    until: EVAL,
    expect: (focused) => (focused === 'canvas' ? '' : `Tab moved the focus to ${focused}`),
  },
  // [rc4l] The screenshot command downloads the frame, since a browser's WebGPU reads nothing back at once and a file in the page's own storage would be out of the player's reach.
  {
    name: 'a screenshot downloads the frame',
    evaluate: inPage(`${pause(2000)} Module.fuaCommand('screenshot');`),
    until: '^\\[log\\] Downloading Screenshot_.*\\.png$',
    download: true,
    expectShot: blankGame,
  },
  // [rc4l] A save's picture reads back a frame or two after the save is written, and goes into it then.
  {
    name: 'a save gets its picture',
    evaluate: inPage(`${pause(2000)} Module.fuaCommand('save /home/web_user/checkpicture.zds'); ${pause(2000)}
      if (!Module.FS.analyzePath('/home/web_user/checkpicture.zds').exists) return { found: false };
      const image = await createImageBitmap(new Blob([Module.FS.readFile('/home/web_user/checkpicture.zds')], { type: 'image/png' }));
      const draw = new OffscreenCanvas(image.width, image.height).getContext('2d');
      draw.drawImage(image, 0, 0);
      const px = draw.getImageData(0, 0, image.width, image.height).data;
      let lo = 255, hi = 0;
      for (let i = 0; i < px.length; i += 4) { const v = (px[i] + px[i + 1] + px[i + 2]) / 3; lo = Math.min(lo, v); hi = Math.max(hi, v); }
      return { found: true, width: image.width, height: image.height, range: Math.round(hi - lo) };`),
    until: EVAL,
    expect: savePicture,
  },
  {
    name: 'quitting stops the loop and offers to play again',
    evaluate: inPage(`
      ${COUNT_FRAMES}
      Module.fuaCommand('screenblocks 11');
      Module.fuaCommand('quit');
      frames = 0;
      const again = document.getElementById('again');
      for (let i = 0; i < 50 && again.hidden; i++) ${pause(100)}
      ${pause(500)}
      return { again: !again.hidden, frames };`),
    until: EVAL,
    expect: (v) => (!v.again ? 'the page did not offer to play again' : v.frames ? `${v.frames} frames ran after quitting` : ''),
  },
  { name: 'a reload keeps what was set before quitting', evaluate: inPage(`Module.fuaCommand('screenblocks');`), until: '"screenblocks" is "11"' },
  {
    name: 'leaving the page stores the config',
    evaluate: inPage(`Module.fuaCommand('screenblocks 9'); window.dispatchEvent(new Event('pagehide')); return await Module.fuaSaving;`),
    until: EVAL,
    expect: (stored) => (stored === true ? '' : 'the store failed'),
  },
  { name: 'a reload keeps what was set before leaving', evaluate: inPage(`Module.fuaCommand('screenblocks');`), until: '"screenblocks" is "9"' },
  {
    name: 'a recoverable error goes to the console and the loop goes on',
    evaluate: inPage(`${COUNT_FRAMES} Module.fuaCommand('error checking'); frames = 0; ${pause(1000)} return { frames, shown: !document.getElementById('error').hidden };`),
    until: EVAL,
    expect: (v) => (v.shown ?'the page showed it as fatal' : !v.frames ? 'no frames ran after it' : ''),
  },
  {
    name: 'a fatal error stops the loop and shows on the page',
    evaluate: inPage(`${COUNT_FRAMES} Module.fuaCommand('error_fatal checking'); frames = 0; ${pause(1000)} const e = document.getElementById('error'); return { frames, shown: e.hidden ? '' : e.textContent };`),
    until: EVAL,
    expect: (v) => (!v.shown.includes('checking') ? 'the page did not show it' : v.frames ? `${v.frames} frames ran after it` : ''),
  },
];

// [rc4l] Whether a check passed: its line matched, an evaluation returned what the check wants, and a screenshot (img, decoded) shows what it should.
export function judge(check, matched, img = null) {
  if (!matched) return { ok: false, reason: `no line matched /${check.until}/` };
  if (matched.startsWith('[eval] threw')) return { ok: false, reason: matched.slice('[eval] '.length) };
  const reason = (check.expect && check.expect(JSON.parse(matched.replace(/^\[eval\] /, ''))))
    || (check.expectShot && (img ? check.expectShot(img) : 'no screenshot'));
  return reason ? { ok: false, reason } : { ok: true };
}

export async function check({ url, checks = CHECKS, seconds = 60, software = false, onResult = console.log }) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-check-'));
  const shots = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-check-shots-'));
  let failed = 0;
  try {
    for (const [i, c] of checks.entries()) {
      const shot = c.shot ? path.join(shots, `check-${i}.png`) : undefined;
      const downloadDir = c.download ? fs.mkdtempSync(path.join(shots, `check-${i}-`)) : undefined;
      const { matched, lines } = await smoke({ url, profile, seconds, software, shot, downloadDir, until: c.until, evaluate: c.evaluate, after: 1, init: c.init, onLine: () => {} });
      const picture = shot || (downloadDir && fs.readdirSync(downloadDir).filter((f) => f.endsWith('.png')).map((f) => path.join(downloadDir, f))[0]);
      const { ok, reason } = judge(c, matched, picture && fs.existsSync(picture) ? decodePNG(picture) : null);
      onResult(`${ok ? 'PASS' : 'FAIL'} ${c.name}${ok ? '' : ': ' + reason}`);
      if (!ok) {
        failed++;
        for (const line of lines.slice(-20)) onResult(`  ${line}`);
        if (shot || downloadDir) onResult(`  screenshot: ${shot || downloadDir}`);
      }
    }
  } finally {
    fs.rmSync(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
    if (!failed) fs.rmSync(shots, { recursive: true, force: true });
  }
  return failed;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const root = args.find((a) => !a.startsWith('--')) ?? 'dist-web';
  const server = createServer(root).listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  const failed = await check({ url: `http://127.0.0.1:${server.address().port}/?map=MAP01`, software: args.includes('--software'), seconds: args.includes('--software') ? 180 : 60 });
  server.close();
  process.exit(failed ? 1 : 0);
}
