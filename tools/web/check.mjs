#!/usr/bin/env node
// [rc4l] The browser build's checks in headless Chrome, in order on one profile: it boots and draws, and a setting survives quitting, leaving and reloading the page.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from './serve.mjs';
import { smoke, gpuLogLines } from './smoke.mjs';
import { decodePNG, boxStats, px } from '../fuactl/src/pngcore.mjs';

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

// [rc4l] The page's line once the engine's screenshot has been downloaded.
const DOWNLOADED = '^\\[log\\] Downloading Screenshot_.*\\.png$';

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

// [rc4l] Why a save did not come back, or '': the file gone after the reload, or the engine refusing it.
export function saveLoads({ found, said }) {
  if (!found) return 'the save was gone after the reload';
  const refusal = /(could not|couldn't|not a valid|different version|cannot).*/i.exec(said);
  return refusal ? `loading it said: ${refusal[0].trim()}` : '';
}

// [rc4l] Why a resized page shows the frame wrongly, or '': its pure green crosshair missing, or away from the middle, as a frame drawn at the old size and cut to the new one put it.
export function crosshairCentred(img) {
  let n = 0, sx = 0, sy = 0;
  for (let y = 0; y < img.h; y++)
    for (let x = 0; x < img.w; x++) {
      const [r, g, b] = px(img, x, y);
      if (g > 200 && r < 80 && b < 80) { n++; sx += x; sy += y; }
    }
  if (!n) return 'no crosshair in the frame';
  const cx = sx / n / img.w;
  return Math.abs(cx - 0.5) > 0.05 ? `the crosshair is at ${Math.round(cx * 100)}% across, not the middle` : '';
}

// [rc4l] The first error the WebGPU device reported (the page logs each one) as a reason, or ''; any one fails a check, since a refused binding stops drawing for good.
export function gpuError(lines) {
  const line = lines.find((l) => l.includes('WebGPU error:'));
  return line ? `the device reported: ${line.slice(line.indexOf('WebGPU error:') + 'WebGPU error:'.length).replace(/\s+/g, ' ').trim().slice(0, 300)}` : '';
}

// [rc4l] Why the canvas was configured wrongly, or '': never, or again after the first time with nothing resized.
export function configuredOnce(n) {
  return n === 1 ? '' : `the canvas was configured ${n} times`;
}

// [rc4l] Why firing at the floor went wrong, or '': the decal pass drawing no mark, which would leave it untried.
export function marked({ draws }) {
  return draws > 0 ? '' : 'the decal pass drew no mark';
}

// [rc4l] Why a crash in a frame was handled wrongly, or '': not shown, no report offered, the loop going on, a report with no frame in the engine, or a build that may not send saying otherwise.
export function crashOffered({ frames, shown, offered, status, stack }) {
  if (!shown.includes('checking a crash')) return 'the page did not show the crash';
  if (!offered) return 'the page offered no report';
  if (frames) return `${frames} frames ran after the crash`;
  if (!stack) return "the report's stack has no frame in the engine";
  return /does not send/.test(status) ? '' : `the page said: ${status}`;
}

// [rc4l] Why a report did not reach the tracker with its game, or '': the engine not telling the page its release, map, files or setting, or the report missing, misaddressed or without the game.
export function reportSent({ release, map, files, off, on, url, event, status }) {
  if (!release || !map) return `the engine told the page release "${release}" and map "${map}"`;
  if (!files.includes('freedoom2.wad')) return `the engine told the page the files ${files.join(', ')}`;
  if (off !== false || on !== true) return `cl_crashreports did not reach the page (off ${off}, on ${on})`;
  if (url !== 'https://127.0.0.1/api/1/envelope/?sentry_key=checking&sentry_version=7') return url ? `the report went to ${url}` : 'no report was sent';
  if (!event || event.level !== 'fatal' || !event.files.includes('freedoom2.wad')) return 'the report sent did not carry the game';
  return /was sent/.test(status) ? '' : `the page said: ${status}`;
}

// [rc4l] An error thrown inside the engine's frame, as a WebGPU call refusing a buffer throws one: the page's screenshot hook, which the frame calls, made to throw, then a screenshot asked for.
const CRASH_IN_FRAME = `Module.fuaCaptureFrame = () => { throw new RangeError('checking a crash'); };
      Module.fuaCommand('screenshot');`;

// [rc4l] Waits for the page's report of a crash, then holds its status line and the reporter's last report.
const READ_REPORT = `const status = document.getElementById('report-status');
      for (let i = 0; i < 50 && (document.getElementById('error').hidden || status.textContent.startsWith('Preparing')); i++) ${pause(100)}
      const last = Module.fuaCrashReporter.last();`;

// [rc4l] Counts the times the page's canvas is sized to zero, as SDL does to the canvas of a window it destroys.
const COUNT_ZERO_CANVAS = `window.fuaZeroCanvas = 0;
  for (const k of ['width', 'height']) {
    const d = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, k);
    Object.defineProperty(HTMLCanvasElement.prototype, k, { configurable: true, get: d.get, set(v) { if (this.id === 'canvas' && !Number(v)) window.fuaZeroCanvas++; d.set.call(this, v); } });
  }`;

export const CHECKS = [
  // [rc4l] Judged from the engine's screenshot, which the page downloads from the canvas just after a frame, since a browser's WebGPU reads nothing back at once; headless Chrome on Linux composites no WebGPU canvas into a screenshot of the page.
  {
    name: 'boots and draws MAP01, and a screenshot downloads the frame',
    evaluate: inPage(`${pause(3000)} Module.fuaCommand('screenshot');`),
    until: DOWNLOADED,
    download: true,
    expectShot: blankGame,
  },
  // [rc4l] Firefox reconfigures its WebGPU context at every size the canvas takes, and refuses a zero one.
  {
    name: 'the canvas never goes to zero size, starting or switching fullscreen',
    init: COUNT_ZERO_CANVAS,
    evaluate: inPage(`${pause(3000)} Module.fuaCommand('fullscreen true'); ${pause(1000)} Module.fuaCommand('fullscreen false'); ${pause(1000)} return window.fuaZeroCanvas;`),
    until: EVAL,
    expect: (n) => (n ? `the canvas was sized to zero ${n} times` : ''),
  },
  // [rc4l] Each configure makes the browser new swap textures, which Chrome on Linux failed to give the page once the first frame configured it again.
  {
    name: 'the canvas is configured once through a level start',
    init: `window.fuaConfigures = 0; {
      const real = GPUCanvasContext.prototype.configure;
      GPUCanvasContext.prototype.configure = function (c) { window.fuaConfigures++; return real.call(this, c); };
    }`,
    evaluate: inPage(`${pause(3000)} return window.fuaConfigures;`),
    until: EVAL,
    expect: configuredOnce,
  },
  // [rc4l] A bullet's mark on the floor goes through the decal pass, which samples the scene's depth.
  {
    name: 'a mark on the floor draws without a device error',
    evaluate: inPage(`
      const log = document.getElementById('console');
      let seen = { boxes: 0, draws: 0 };
      // [rc4l] A few tries, since a software adapter can be slow enough that the level is not in play yet.
      for (let i = 0; i < 5 && !seen.draws; i++) {
        ${pause(1000)}
        Module.fuaCommand('+lookdown');
        ${pause(700)}
        Module.fuaCommand('-lookdown');
        Module.fuaCommand('+attack');
        ${pause(800)}
        Module.fuaCommand('-attack');
        ${pause(1000)}
        const before = log.textContent.length;
        Module.fuaCommand('fua_projdecals_stats');
        ${pause(300)}
        const pass = /deferred pass: (\\d+) boxes in (\\d+) draws/.exec(log.textContent.slice(before));
        if (pass) seen = { boxes: Number(pass[1]), draws: Number(pass[2]) };
      }
      return seen;`),
    until: EVAL,
    expect: marked,
  },
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
  // [rc4l] The canvas follows the page's layout, and the frame follows the canvas rather than being cut to it.
  {
    name: 'the frame follows the page when it is resized',
    evaluate: inPage(`
      Module.fuaCommand('crosshair 1');
      Module.fuaCommand('crosshaircolor "00 ff 00"');
      console.log('check: resize 900x700');
      ${pause(1500)}
      Module.fuaCommand('screenshot');`),
    until: DOWNLOADED,
    download: true,
    expectShot: crosshairCentred,
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
  // [rc4l] A save's picture reads back a frame or two after the save is written, and goes into it then; a software adapter can take several seconds, so the check waits for it.
  {
    name: 'a save gets its picture',
    evaluate: inPage(`${pause(2000)} Module.fuaCommand('save /home/web_user/checkpicture.zds');
      const file = '/home/web_user/checkpicture.zds';
      let image = null;
      for (let i = 0; i < 40 && !(image && image.width > 1); i++) {
        ${pause(250)}
        if (Module.FS.analyzePath(file).exists) image = await createImageBitmap(new Blob([Module.FS.readFile(file)], { type: 'image/png' })).catch(() => null);
      }
      if (!image) return { found: false };
      const draw = new OffscreenCanvas(image.width, image.height).getContext('2d');
      draw.drawImage(image, 0, 0);
      const px = draw.getImageData(0, 0, image.width, image.height).data;
      let lo = 255, hi = 0;
      for (let i = 0; i < px.length; i += 4) { const v = (px[i] + px[i + 1] + px[i + 2]) / 3; lo = Math.min(lo, v); hi = Math.max(hi, v); }
      window.dispatchEvent(new Event('pagehide'));
      await Module.fuaSaving;
      return { found: true, width: image.width, height: image.height, range: Math.round(hi - lo) };`),
    until: EVAL,
    expect: savePicture,
  },
  // [rc4l] The save the check above stored, there after a reload and loading without complaint.
  {
    name: 'a save survives a reload and loads',
    evaluate: inPage(`
      if (!Module.FS.analyzePath('/home/web_user/checkpicture.zds').exists) return { found: false };
      const log = document.getElementById('console');
      const before = log.textContent.length;
      Module.fuaCommand('load /home/web_user/checkpicture.zds');
      ${pause(1500)}
      return { found: true, said: log.textContent.slice(before) };`),
    until: EVAL,
    expect: saveLoads,
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
  {
    name: 'a crash in a frame stops the game and offers its report',
    evaluate: inPage(`
      ${COUNT_FRAMES}
      ${pause(1500)}
      ${CRASH_IN_FRAME}
      ${READ_REPORT}
      frames = 0;
      ${pause(500)}
      return { frames, shown: document.getElementById('error-text').textContent, offered: !document.getElementById('report').hidden, status: status.textContent,
        stack: !!last && last.event.exception.values[0].stacktrace.frames.some((f) => f.filename.endsWith('forkundera.wasm')) };`),
    until: EVAL,
    expect: crashOffered,
  },
  // [rc4l] The checks' build may not send, so this one hands the page a DSN, and keeps what would leave the page.
  {
    name: 'a crash report reaches the tracker with the game it happened in',
    init: `window.fuaSent = []; {
      const real = window.fetch;
      window.fetch = (url, init) => String(url).includes('/envelope/') ? (window.fuaSent.push({ url: String(url), body: init.body }), Promise.resolve(new Response('{}'))) : real(url, init);
    }`,
    evaluate: inPage(`
      ${pause(1500)}
      const reporter = Module.fuaCrashReporter;
      const told = reporter.context();
      Module.fuaCommand('cl_crashreports 0');
      const off = reporter.context().reporting;
      Module.fuaCommand('cl_crashreports 1');
      const on = reporter.context().reporting;
      reporter.setContext({ ...reporter.context(), dsn: 'https://checking@127.0.0.1/1' });
      ${CRASH_IN_FRAME}
      ${READ_REPORT}
      const sent = window.fuaSent[0];
      const event = sent ? JSON.parse(sent.body.split('\\n')[2]) : null;
      return { release: told.release || '', map: told.map || '', files: told.files || [], off, on, url: sent ? sent.url : '',
        event: event && { level: event.level, files: event.extra.load_order }, status: status.textContent };`),
    until: EVAL,
    expect: reportSent,
  },
];

// [rc4l] Whether a check passed: its line matched, an evaluation returned what the check wants, a screenshot (img, decoded) shows what it should, and its lines hold no device error.
export function judge(check, matched, img = null, lines = []) {
  if (!matched) return { ok: false, reason: `no line matched /${check.until}/` };
  if (matched.startsWith('[eval] threw')) return { ok: false, reason: matched.slice('[eval] '.length) };
  const reason = gpuError(lines)
    || (check.expect && check.expect(JSON.parse(matched.replace(/^\[eval\] /, ''))))
    || (check.expectShot && (img ? check.expectShot(img) : 'no screenshot'));
  return reason ? { ok: false, reason } : { ok: true };
}

// [rc4l] The checks whose names contain `only`, or all of them; a later check may need the state an earlier one left.
export function selectChecks(checks, only) {
  return only ? checks.filter((c) => c.name.includes(only)) : checks;
}

export async function check({ url, checks = CHECKS, seconds = 60, software = false, onResult = console.log }) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-check-'));
  const shots = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-check-shots-'));
  let failed = 0;
  try {
    for (const [i, c] of checks.entries()) {
      const shot = c.shot ? path.join(shots, `check-${i}.png`) : undefined;
      const downloadDir = c.download ? fs.mkdtempSync(path.join(shots, `check-${i}-`)) : undefined;
      const logFile = path.join(shots, `check-${i}-chrome.log`);
      const { matched, lines, version } = await smoke({ url, profile, seconds, software, shot, downloadDir, logFile, until: c.until, evaluate: c.evaluate, after: 1, init: c.init, onLine: () => {} });
      if (i === 0) onResult(`browser: ${version}`);
      const picture = shot || (downloadDir && fs.readdirSync(downloadDir).filter((f) => f.endsWith('.png')).map((f) => path.join(downloadDir, f))[0]);
      const { ok, reason } = judge(c, matched, picture && fs.existsSync(picture) ? decodePNG(picture) : null, lines);
      onResult(`${ok ? 'PASS' : 'FAIL'} ${c.name}${ok ? '' : ': ' + reason}`);
      if (!ok) {
        failed++;
        for (const line of lines.slice(-20)) onResult(`  ${line}`);
        for (const line of gpuLogLines(fs.readFileSync(logFile, 'utf8'))) onResult(`  [chrome] ${line}`);
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
  const only = args.includes('--only') ? args[args.indexOf('--only') + 1] : null;
  const root = args.find((a, i) => !a.startsWith('--') && args[i - 1] !== '--only') ?? 'dist-web';
  const checks = selectChecks(CHECKS, only);
  if (!checks.length) { console.error(`no check's name contains "${only}"`); process.exit(2); }
  const server = createServer(root).listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  const failed = await check({
    url: `http://127.0.0.1:${server.address().port}/?map=MAP01`,
    checks,
    software: args.includes('--software'),
    seconds: args.includes('--software') ? 180 : 60,
  });
  server.close();
  process.exit(failed ? 1 : 0);
}
