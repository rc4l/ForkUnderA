#!/usr/bin/env node
// [rc4l] The browser build's checks in headless Chrome, in order on one profile (or those marked firefox in Firefox): it boots and draws, and a setting survives quitting, leaving and reloading the page.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveLocally } from './serve.mjs';
import { gpuLogLines, removeQuietly } from './smoke.mjs';
import { smokeIn } from './firefox.mjs';
import { WATCH_SOUNDS, WATCH_TEXTURES, WATCH_LEVEL, HEAR_CONSOLE, PIECE_S, GRAB_FRAME, inPage, pause, serveData, soakInPage } from './page.mjs';
import { decodePNG, decodePNGBytes, boxDiff, boxDiffMax, boxStats, px } from '../fuactl/src/pngcore.mjs';
import { buildPointlightWad, VANTAGES as POINTLIGHT_VANTAGES, BOXES as POINTLIGHT_BOXES } from '../fuactl/scenarios/pointlight.mjs';

// [rc4l] Counts the animation frames the page asks for from here on, which stop when the engine's loop does.
const COUNT_FRAMES = `let frames = 0;
      const raf = window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame = (f) => { frames++; return raf(f); };`;

// [rc4l] How loud the shooters must come out against the sound with no distance: the bug left them silent, and their own peak follows which of them fire and when, so an absolute 0.05 failed a CI run at 0.0475 against 0.0928.
export const HEARD_SHARE = 0.25;

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
  if (!played) return 'nothing played in the six seconds after the click';
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

// [rc4l] The pointlight scenario's hall, as the light grid check serves it to the page.
const POINTLIGHT_WAD = 'fua-pointlight.wad';

// [rc4l] The pointlight scenario's vantages the browser frames, its nearest and a middle one, since each frame takes seconds under CI's software WebGPU and the scenario covers the rest.
export const LIGHT_VANTAGES = POINTLIGHT_VANTAGES.filter((v) => ['d600', 'd1500'].includes(v.name));

// [rc4l] How much one block of the frame must change with the light on for the comparison to mean anything.
export const LIT_FLOOR = 4;

// [rc4l] Why the light grid check failed, or '': at each vantage the light changed the frame, and the grid drew what no grid draws, judged as the pointlight scenario judges it.
export function gridMatches(at) {
  const { box, ceiling } = POINTLIGHT_BOXES.MAP01.frame;
  for (const { name, grid, none, dark } of at) {
    const [g, n, d] = [grid, none, dark].map((b64) => decodePNGBytes(Buffer.from(b64, 'base64')));
    const lit = boxDiffMax(n, d, box);
    if (!(lit >= LIT_FLOOR)) return `at ${name} the light changed no block by more than ${lit.toFixed(2)}, so it never drew`;
    const off = boxDiff(g, n, box);
    if (!(off <= ceiling)) return `at ${name} the light grid drew ${off.toFixed(3)} away from no grid, over ${ceiling}`;
  }
  return '';
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

// [rc4l] Why a game that can no longer draw was handled wrongly, or '': not shown as out of memory, no report offered, or the loop going on.
export function stoppedOffered({ frames, shown, offered }) {
  if (!shown.includes('out of graphics memory')) return `the page showed "${shown}"`;
  if (!offered) return 'the page offered no report';
  return frames ? `${frames} frames ran after it` : '';
}

// [rc4l] Why a sound played wrongly, or '': never started, or started again after it ended.
export function playedOnce(n) {
  return n === 1 ? '' : n ? `the sound started ${n} times` : 'the sound never started';
}

// [rc4l] Why a format made no sound, or '': each format's command and how many sounds it started.
export function everyFormatPlayed(started) {
  const silent = Object.keys(started).filter((format) => !started[format]);
  return silent.length ? `nothing played in ${silent.join(', ')}` : '';
}

// [rc4l] Why the game thread's check failed, or '': no tic ran on its own thread, or none of the shots it fired made a sound.
export function threadedWithSound({ tics, sounds }) {
  return !tics ? 'no tic ran on the game thread' : !sounds ? 'the shots fired on the game thread made no sound' : '';
}

// [rc4l] Why resizing held more of the GPU than one size does, or '': more screen-sized textures alive than before, past one more set of the screen's targets.
export function heldOnce({ before, after }) {
  return after - before > 6 ? `${after - before} more screen-sized textures were held after the resizes (${before} before)` : '';
}

// [rc4l] Why repeating an action kept memory, or '': the engine's soak verdict (fua_soak), which judges every count of bytes, objects, handles and threads it reads.
export function soakHeld({ pass, marks, judged, grew, kept }) {
  if (pass) return '';
  const labels = kept && kept.length ? `; the page's WebGPU objects grew in: ${kept.join(', ')}` : '';
  return grew && grew.length ? `kept growing with each repeat: ${grew.map((g) => g.text).join('; ')}${labels}` : `the soak judged nothing (${marks} marks, ${judged} counters)`;
}

// [rc4l] Why the page held more of a tag than the engine (fua_stats json gpu/), or '': a buffer or texture the engine let go of and never destroyed, waiting on the garbage collector.
export function destroyedAsReleased(rows) {
  const value = new Map(rows.map((r) => [r.name, r.last]));
  const extra = [];
  for (const r of rows) {
    if (!r.name.startsWith('gpu/page/fua')) continue;
    const engine = value.get('gpu/mem/tag/' + r.name.slice('gpu/page/'.length)) ?? 0;
    if (r.last > engine * 1.1 + 65536) extra.push(`${r.name.slice('gpu/page/'.length)} ${Math.round(r.last / 1024)} KB held against the engine's ${Math.round(engine / 1024)} KB`);
  }
  if (!rows.length) return 'no GPU readings';
  return extra.length ? `the page held what the engine let go of: ${extra.join('; ')}` : '';
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
  // [rc4l] Firefox's WebGPU gave gl_FragCoord.w as w rather than 1/w, so every fragment read the light grid's nearest depth slice and a distant light lit nothing (queue #68).
  {
    name: 'a distant light draws the same through the light grid as without it',
    firefox: true,
    query: `file=${POINTLIGHT_WAD}&map=MAP01`,
    init: serveData(POINTLIGHT_WAD, buildPointlightWad()),
    evaluate: inPage(`${GRAB_FRAME}
      const frame = async (command) => { Module.fuaCommand(command); ${pause(300)} return grabFrame(); };
      ${pause(3000)}
      const at = [];
      for (const v of ${JSON.stringify(LIGHT_VANTAGES.map(({ name, at: { x, y } }) => ({ name, x, y })))}) {
        Module.fuaCommand('warp ' + v.x + ' ' + v.y);
        ${pause(1000)}
        at.push({ name: v.name, grid: await frame('fua_dg_clusters 1'), none: await frame('fua_dg_clusters 0'), dark: await frame('fua_dg_dynlights 0') });
        Module.fuaCommand('fua_dg_dynlights 1');
        Module.fuaCommand('fua_dg_clusters 1');
      }
      return at;`),
    until: EVAL,
    expect: gridMatches,
  },
  {
    name: 'the sound waits for a click and plays after one',
    init: `window.fuaAudio = []; window.fuaBuffers = 0; {
      const AC = window.AudioContext; window.AudioContext = class extends AC { constructor(...a) { super(...a); window.fuaAudio.push(this); } };
      const make = BaseAudioContext.prototype.createBufferSource; BaseAudioContext.prototype.createBufferSource = function (...a) { window.fuaBuffers++; return make.apply(this, a); };
    }`,
    // [rc4l] Tics inline: this page is given no animation frame after the click's own, and a tic on the game thread resumes the level's sound a tic later, in the next.
    evaluate: inPage(`
      Module.fuaCommand('fua_gamethread 0');
      const before = window.fuaAudio.map((c) => c.state);
      console.log('check: click');
      for (let i = 0; i < 50 && !window.fuaAudio.every((c) => c.state === 'running'); i++) ${pause(100)}
      const buffers = window.fuaBuffers;
      // [rc4l] Waits for the first sound rather than counting a fixed window, since the click can land in the level's first frames, each over a second on a software adapter.
      for (let i = 0; i < 60 && window.fuaBuffers === buffers; i++) ${pause(100)}
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
  // [rc4l] Each size makes the screen's targets again, and a browser frees the old ones only when its garbage collector runs; Firefox ran out of GPU memory part way through a window being dragged bigger.
  {
    name: 'resizing the page many times holds no more of the GPU than once',
    init: WATCH_TEXTURES,
    evaluate: inPage(`
      ${pause(2000)}
      const before = window.fuaHeld.size;
      for (let i = 0; i < 20; i++) { console.log('check: resize ' + (900 + i * 23) + 'x' + (600 + i * 17)); ${pause(250)} }
      ${pause(1500)}
      return { before, after: window.fuaHeld.size };`),
    until: EVAL,
    expect: heldOnce,
  },
  // [rc4l] Each level load uploads the level and its textures again, and a browser frees a released buffer only when its garbage collector runs; Firefox ran out of GPU memory on a map loaded the third time.
  // [rc4l] Judged after three levels, since the page's heap settles over the first few (the sprite atlas keeps its pages from one to the next) and flat after; twelve reloads showed no growth past them.
  {
    name: 'reloading the level holds no more memory',
    evaluate: soakInPage('reload', 9, 2500, 'MAP01', 3),
    until: EVAL,
    expect: soakHeld,
  },
  // [rc4l] Released buffers and textures are destroyed after the next frame rather than left to the garbage collector; a browser numbers buffers and textures apart, and a buffer sharing a kept texture's number went unkept (queue #48).
  {
    name: 'what a level change lets go of is destroyed, not left to the collector',
    evaluate: inPage(`
      ${pause(2000)}
      Module.fuaCommand('map MAP01');
      ${pause(3000)}
      ${HEAR_CONSOLE}
      Module.fuaCommand('fua_stats json gpu/');
      unhear();
      return lastJson('[', []);`),
    until: EVAL,
    expect: destroyedAsReleased,
  },
  // [rc4l] A wipe draws the console and menu once a step; they piled up under one 2D generation and were drawn past the 2D's vertices, which WebGPU refused for the whole frame.
  {
    name: 'a map change wipes to the new map without a device error',
    evaluate: inPage(`${pause(2000)} Module.fuaCommand('map MAP02'); ${pause(4000)} Module.fuaCommand('screenshot');`),
    until: DOWNLOADED,
    download: true,
    expectShot: blankGame,
  },
  // [rc4l] A page has no restart of its own, so wad_reload reloads it, which started on the page's own arguments and quietly kept the old WAD set.
  {
    name: 'wad_reload reloads the page onto the new set and map',
    evaluate: inPage(`${pause(2000)} Module.fuaCommand('wad_reload /freedoom2.wad map=MAP02'); return 'reloading';`),
    until: '^\\[log\\] map02 - ',
  },
  // [rc4l] A browser's OpenAL reports a finished sound's whole length, often a sample short, so the channel took it for cut off and played it again: Doom 2's rocket blast after every BFG ball, its doors, the shotgun's cock; Freedoom's brain cube is one such sound.
  {
    name: 'a sound plays once',
    autoplay: true,
    init: WATCH_SOUNDS,
    evaluate: inPage(`
      ${pause(1500)}
      const from = window.fuaSounds.length;
      Module.fuaCommand('playsound brain/cube');
      ${pause(3500)}
      const sounds = window.fuaSounds.slice(from).filter((s) => s.len > 1);
      return sounds.length ? sounds.filter((s) => s.id === sounds[0].id).length : 0;`),
    until: EVAL,
    expect: playedOnce,
  },
  // [rc4l] The browser build has no libsndfile or mpg123, so before features/sound-decoders every sound not in Doom's own format was silent, the engine's own secret, connect and taunt sounds among them.
  {
    name: 'FLAC, Ogg Vorbis and WAV sounds play',
    autoplay: true,
    init: WATCH_SOUNDS,
    evaluate: inPage(`
      ${pause(1500)}
      const started = {};
      for (const [format, command] of [['FLAC', 'playsound misc/secret'], ['Ogg Vorbis', 'playsound zandronum/connect'], ['WAV', 'taunt']]) {
        const from = window.fuaSounds.length;
        Module.fuaCommand(command);
        ${pause(1500)}
        started[format] = window.fuaSounds.slice(from).filter((s) => s.len >= ${PIECE_S}).length;
      }
      return started;`),
    until: EVAL,
    expect: everyFormatPlayed,
  },
  // [rc4l] fua_gamethread: the tics run on a worker beside the page's frames, and the sounds a tic starts, here the pistol's, still play, started by the handoff on the page thread.
  {
    name: 'the game thread runs the tics and their sounds play',
    autoplay: true,
    init: WATCH_SOUNDS,
    evaluate: inPage(`
      ${HEAR_CONSOLE}
      Module.fuaCommand('fua_gamethread 1');
      ${pause(1500)}
      const from = window.fuaSounds.length;
      Module.fuaCommand('+attack');
      ${pause(1200)}
      Module.fuaCommand('-attack');
      ${pause(400)}
      Module.fuaCommand('fua_gamethread_report');
      ${pause(300)}
      unhear();
      const line = said.slice().reverse().find((l) => l.includes('game thread:')) || '';
      const m = /game thread: (\\d+) tics/.exec(line);
      return { tics: m ? Number(m[1]) : 0, sounds: window.fuaSounds.slice(from).length };`),
    until: EVAL,
    expect: threadedWithSound,
  },
  // [rc4l] SDL scales mouse motion by its window over the canvas, axis by axis, and learned the canvas's size only from the window's resize event, so fullscreen left the axes scaled apart; its window must match the canvas there and after.
  {
    name: 'the mouse is scaled alike on both axes through fullscreen',
    focused: true,
    evaluate: inPage(`
      ${HEAR_CONSOLE}
      const cvar = async (name) => { const m = /is "([^"]*)"/.exec((await ask(name, 200)).join(' ')); return m ? Number(m[1]) : NaN; };
      const apart = async () => {
        const r = document.getElementById('canvas').getBoundingClientRect();
        return Math.max(Math.abs((await cvar('vid_defwidth')) - r.width), Math.abs((await cvar('vid_defheight')) - r.height));
      };
      // [rc4l] Each read until it settles, since on a software adapter one frame can outlast any fixed wait and the canvas's new size reaches SDL a frame later.
      const settled = async () => {
        let d = await apart();
        for (const end = Date.now() + 10000; d > 1 && Date.now() < end; d = await apart()) { ${pause(250)} }
        return d;
      };
      ${pause(1500)}
      console.log('check: click fullscreen');
      for (const end = Date.now() + 10000; !document.fullscreenElement && Date.now() < end;) { ${pause(100)} }
      const entered = !!document.fullscreenElement;
      const full = await settled();
      await document.exitFullscreen().catch(() => {});
      const back = await settled();
      unhear();
      return { entered, full, back };`),
    until: EVAL,
    expect: ({ entered, full, back }) => (!entered ? 'the page never went fullscreen'
      : full <= 1 && back <= 1 ? '' : `SDL's window was ${full.toFixed(1)} px off the canvas in fullscreen and ${back.toFixed(1)} after`),
  },
  // [rc4l] A sound a tic on the game thread started kept no rolloff for its later start, so everything heard from a distance came out silent while every start still counted; shooters across the room must be heard.
  {
    name: 'sounds the game thread starts are heard at a distance',
    autoplay: true,
    init: WATCH_LEVEL,
    evaluate: inPage(`
      Module.fuaCommand('fua_gamethread 1');
      Module.fuaCommand('god');
      Module.fuaCommand('snd_musicvolume 0');
      ${pause(1000)}
      // [rc4l] A sound with no distance first, then quiet again before the shooters come.
      window.fuaLevel.reset();
      Module.fuaCommand('playsound brain/cube');
      for (const end = Date.now() + 10000; Date.now() < end && window.fuaLevel.peak() <= 0.05;) { ${pause(250)} }
      const control = Number(window.fuaLevel.peak().toFixed(4));
      ${pause(3000)}
      for (const m of ['ShotgunGuy', 'ShotgunGuy', 'ChaingunGuy']) Module.fuaCommand('summon ' + m + ' 256 0');
      ${pause(1500)}
      window.fuaLevel.reset();
      // [rc4l] Listened to until a shot is heard or twenty seconds pass, since a software adapter runs the shooters' tics a frame of a second or more apart.
      for (const end = Date.now() + 20000; Date.now() < end && window.fuaLevel.peak() <= control * ${HEARD_SHARE};) { ${pause(250)} }
      return { control, peak: Number(window.fuaLevel.peak().toFixed(4)), contexts: window.fuaLevel.contexts() };`),
    until: EVAL,
    expect: (heard) => heardAtDistance(heard),
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
  // [rc4l] Game keys did nothing until a frame off the game thread's schedule built SDL's key table, which fullscreen happened to cause; holding up must move the player.
  {
    name: 'a game key moves the player',
    evaluate: inPage(`
      ${HEAR_CONSOLE}
      const at = async () => {
        const m = /\\((-?[\\d.]+),(-?[\\d.]+)/.exec((await ask('currentpos')).find((l) => l.includes('position')) || '');
        return m ? [Number(m[1]), Number(m[2])] : null;
      };
      document.getElementById('canvas').focus();
      ${pause(5000)}
      const a = await at();
      // [rc4l] Held until the player moves or twenty seconds pass: a fixed hold came and went inside one frame of a software adapter, so the game saw a single tic of it.
      console.log('check: down ArrowUp');
      let moved = 0;
      for (const end = Date.now() + 20000; a && moved <= 32 && Date.now() < end;) {
        ${pause(500)}
        const b = await at();
        if (b) moved = Math.hypot(b[0] - a[0], b[1] - a[1]);
      }
      console.log('check: up ArrowUp');
      unhear();
      return a ? Math.round(moved) : -1;`),
    until: EVAL,
    expect: (moved) => (moved > 32 ? '' : `holding up moved the player ${moved} units`),
  },
  // [rc4l] A save's picture reads back a frame or two after the save is written, and goes into it then; a software adapter can take several seconds, so the check waits for it.
  // [rc4l] Saved by a bare name, as a player types it, which lands in the save directory the page keeps.
  {
    name: 'a save gets its picture',
    evaluate: inPage(`${pause(2000)} Module.fuaCommand('save checkpicture');
      let image = null;
      for (let i = 0; i < 40 && !(image && image.width > 1); i++) {
        ${pause(250)}
        const file = FuaHome.find(Module.FS, 'checkpicture.zds');
        if (file) image = await createImageBitmap(new Blob([Module.FS.readFile(file)], { type: 'image/png' })).catch(() => null);
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
      if (!FuaHome.find(Module.FS, 'checkpicture.zds')) return { found: false };
      const log = document.getElementById('console');
      const before = log.textContent.length;
      Module.fuaCommand('load checkpicture');
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
  // [rc4l] Out of graphics memory, what was being made is unusable and every frame is refused, which left the last frame on the canvas with no panel; the device reports one as the page's own error does.
  {
    name: 'a game that can no longer draw stops and offers its report',
    deviceError: true,
    evaluate: inPage(`
      ${COUNT_FRAMES}
      ${pause(1500)}
      Module.preinitializedWebGPUDevice.dispatchEvent(new GPUUncapturedErrorEvent('uncapturederror', { error: new GPUOutOfMemoryError('checking out of memory') }));
      ${READ_REPORT}
      frames = 0;
      ${pause(500)}
      return { frames, shown: document.getElementById('error-text').textContent, offered: !document.getElementById('report').hidden };`),
    until: EVAL,
    expect: stoppedOffered,
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

// [rc4l] Whether a check passed: its line matched, an evaluation returned what the check wants, a screenshot (img, decoded) shows what it should, and its lines hold no device error unless it makes one (deviceError); an expect answering { skip } is a check this browser cannot judge, with the reason.
export function judge(check, matched, img = null, lines = []) {
  if (!matched) return { ok: false, reason: `no line matched /${check.until}/` };
  if (matched.startsWith('[eval] threw')) return { ok: false, reason: matched.slice('[eval] '.length) };
  const reason = (check.deviceError ? '' : gpuError(lines))
    || (check.expect && check.expect(JSON.parse(matched.replace(/^\[eval\] /, ''))))
    || (check.expectShot && (img ? check.expectShot(img) : 'no screenshot'));
  if (reason && reason.skip) return { ok: true, skip: reason.skip };
  return reason ? { ok: false, reason } : { ok: true };
}

// [rc4l] Shooters across the room heard, judged only where a sound with no distance (control) was heard at all: a runner whose browser makes no sound cannot tell a silent distance from silence.
export function heardAtDistance({ control, peak, contexts }) {
  if (!(control > 0.05)) return { skip: `the page made no sound at all, a sound with no distance peaking at ${control} RMS (audio ${JSON.stringify(contexts)})` };
  return peak > control * HEARD_SHARE ? '' : `shooters across the room came out at ${peak} RMS at most, while a sound with no distance peaked at ${control}`;
}

// [rc4l] The checks whose names contain `only`, or all of them; a later check may need the state an earlier one left.
export function selectChecks(checks, only) {
  return only ? checks.filter((c) => c.name.includes(only)) : checks;
}

// [rc4l] The page a check opens: the run's own, or the run's with the check's query in place of its own.
export function checkUrl(url, c) {
  if (!c.query) return url;
  const u = new URL(url);
  u.search = c.query;
  return u.href;
}

// [rc4l] Why a check does not run in this browser, or '': the Firefox driver has no input, screenshot or carried profile, so only a check marked firefox runs there.
export function notIn(browser, c) {
  return browser === 'firefox' && !c.firefox ? 'it needs what only the Chrome driver does' : '';
}

export async function check({ url, checks = CHECKS, seconds = 60, software = false, throttle = 0, browser = 'chrome', onResult = console.log }) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-check-'));
  const shots = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-check-shots-'));
  const run = smokeIn(browser);
  let failed = 0, named = false;
  try {
    for (const [i, c] of checks.entries()) {
      const elsewhere = notIn(browser, c);
      if (elsewhere) { onResult(`SKIP ${c.name}: ${elsewhere}`); continue; }
      const shot = c.shot ? path.join(shots, `check-${i}.png`) : undefined;
      const downloadDir = c.download ? fs.mkdtempSync(path.join(shots, `check-${i}-`)) : undefined;
      const logFile = path.join(shots, `check-${i}-chrome.log`);
      const { matched, lines, version } = await run({ url: checkUrl(url, c), profile, seconds, software, throttle, autoplay: !!c.autoplay, focus: !!c.focused, shot, downloadDir, logFile, until: c.until, evaluate: c.evaluate, after: 1, init: c.init, onLine: () => {} });
      if (!named) { onResult(`browser: ${version}`); named = true; }
      const picture = shot || (downloadDir && fs.readdirSync(downloadDir).filter((f) => f.endsWith('.png')).map((f) => path.join(downloadDir, f))[0]);
      const { ok, reason, skip } = judge(c, matched, picture && fs.existsSync(picture) ? decodePNG(picture) : null, lines);
      onResult(`${skip ? 'SKIP' : ok ? 'PASS' : 'FAIL'} ${c.name}${skip ? ': ' + skip : ok ? '' : ': ' + reason}`);
      if (!ok) {
        failed++;
        for (const line of lines.slice(-20)) onResult(`  ${line.slice(0, 300)}`);
        if (fs.existsSync(logFile)) for (const line of gpuLogLines(fs.readFileSync(logFile, 'utf8'))) onResult(`  [chrome] ${line}`);
        if (shot || downloadDir) onResult(`  screenshot: ${shot || downloadDir}`);
      }
    }
  } finally {
    removeQuietly(profile);
    if (!failed) removeQuietly(shots);
  }
  return failed;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const only = args.includes('--only') ? args[args.indexOf('--only') + 1] : null;
  // [rc4l] --throttle N slows the page's own thread N times, as a slow runner's is, to reproduce CI's timing here.
  const throttle = args.includes('--throttle') ? Number(args[args.indexOf('--throttle') + 1]) : 0;
  // [rc4l] --browser firefox runs the checks marked firefox in Firefox (firefox.mjs), whose WebGPU differs from Chrome's.
  const browser = args.includes('--browser') ? args[args.indexOf('--browser') + 1] : 'chrome';
  const root = args.find((a, i) => !a.startsWith('--') && !['--only', '--throttle', '--browser'].includes(args[i - 1])) ?? 'dist-web';
  const checks = selectChecks(CHECKS, only);
  if (!checks.length) { console.error(`no check's name contains "${only}"`); process.exit(2); }
  const { server, origin } = await serveLocally(root);
  const failed = await check({
    url: `${origin}/?map=MAP01`,
    checks,
    software: args.includes('--software'),
    throttle,
    browser,
    seconds: args.includes('--software') || throttle > 1 ? 180 : 60,
  });
  server.close();
  process.exit(failed ? 1 : 0);
}
