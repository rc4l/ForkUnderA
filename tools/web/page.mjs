// [rc4l] Code for a page, shared by the checks and the probe: the watches run before its own scripts, and what runs once the engine has started.
import { soakSteps } from '../fuactl/src/telemetry.mjs';

// [rc4l] JavaScript run in the page once the engine has started (fua.js sets fuaCommand just before main), its value the caller's result.
export function inPage(body) {
  return `(async () => { while (!(window.Module && Module.fuaCommand)) await new Promise((r) => setTimeout(r, 100)); ${body} })()`;
}

// [rc4l] Page code that waits ms milliseconds.
export const pause = (ms) => `await new Promise((r) => setTimeout(r, ${ms}));`;

// [rc4l] Page code that keeps every console line from here on in said until unhear(); lastJson(start, none) parses the newest one starting with start, and ask(command, ms) is what a command printed within ms.
export const HEAR_CONSOLE = `const said = [];
      const print = console.log;
      console.log = (...a) => { said.push(a.join(' ')); return print.apply(console, a); };
      const unhear = () => { console.log = print; };
      const lastJson = (start, none) => { const line = said.slice().reverse().find((l) => l.startsWith(start)); return line ? JSON.parse(line) : none; };
      const ask = async (command, ms = 300) => { const from = said.length; Module.fuaCommand(command); ${pause('ms')} return said.slice(from); };`;

// [rc4l] Page code that repeats a soak action's steps (fuactl's soakSteps) cycles times on map, marking memory after each cycle once each level a step started has printed its title, or a moment has passed, and the page's garbage is collected (window.gc, with --expose-gc), and returns the engine's verdict on the cycles after warmup.
export function soakInPage(action, cycles, settleMs, map = 'MAP01', warmup = 1) {
  return inPage(`
      ${pause(2000)}
      ${HEAR_CONSOLE}
      const started = async (from, title) => { for (let t = 0; t < 600 && !said.slice(from).some((l) => l.trim().toLowerCase().startsWith(title)); t++) ${pause(100)} };
      const collect = async () => { for (let k = 0; k < 2 && window.gc; k++) { window.gc(); ${pause(200)} } };
      const steps = ${JSON.stringify(soakSteps(action, map))};
      // [rc4l] The page's WebGPU objects by label at the start and the end, so a failing soak shows which ones it kept.
      const held = (when) => console.log('soak gpu ' + when + ': ' + (Module.fuaGpuLedger ? Module.fuaGpuLedger.tagText().replace(/\\n/g, '; ') : 'no ledger'));
      Module.fuaCommand('fua_soak begin');
      held('start');
      for (let i = 0; i < ${cycles}; i++) {
        for (const step of steps) {
          const from = said.length;
          // [rc4l] A "check:" step is asked of the browser (smoke.mjs's inputAsked, a resize or a click) rather than of the engine.
          if (step.text.startsWith('check: ')) console.log(step.text);
          else Module.fuaCommand(step.text);
          if (step.map) await started(from, step.map.toLowerCase() + ' - ');
          else ${pause(300)}
        }
        ${pause(settleMs)}
        await collect();
        Module.fuaCommand('fua_soak mark');
      }
      held('end');
      Module.fuaCommand('fua_soak verdict ${warmup} json');
      unhear();
      return lastJson('{"pass"', {});`);
}

// [rc4l] Every audio buffer the page starts, in window.fuaSounds as { t, id, len }: the audio clock's time, the buffer's number by identity, and its length in seconds.
export const WATCH_SOUNDS = `window.fuaSounds = []; {
    const ids = new WeakMap();
    let next = 0;
    const start = AudioBufferSourceNode.prototype.start;
    AudioBufferSourceNode.prototype.start = function (...a) {
      const b = this.buffer;
      if (b && !ids.has(b)) ids.set(b, ++next);
      window.fuaSounds.push({ t: this.context.currentTime, id: b ? ids.get(b) : 0, len: b ? b.duration : 0 });
      return start.apply(this, a);
    };
  }`;

// [rc4l] The screen-sized textures the page holds, made and neither destroyed nor collected, in window.fuaHeld; the level's own, which a software adapter is still making, are far smaller.
export const WATCH_TEXTURES = `window.fuaHeld = new Set(); {
    const collected = new FinalizationRegistry((id) => window.fuaHeld.delete(id));
    let next = 0;
    const create = GPUDevice.prototype.createTexture;
    GPUDevice.prototype.createTexture = function (d) {
      const t = create.call(this, d);
      const s = d.size, w = s.width ?? s[0], h = s.height ?? s[1] ?? 1;
      if (w * h >= 200000) { t.fuaId = ++next; window.fuaHeld.add(t.fuaId); collected.register(t, t.fuaId); }
      return t;
    };
    const destroy = GPUTexture.prototype.destroy;
    GPUTexture.prototype.destroy = function () { window.fuaHeld.delete(this.fuaId); return destroy.call(this); };
  }`;

// [rc4l] Each sound started more than once, by buffer, with its length and the times it started: the tell of a sound played again.
export function repeatedSounds(sounds) {
  const byId = new Map();
  for (const s of sounds) {
    if (!byId.has(s.id)) byId.set(s.id, { id: s.id, len: s.len, times: [] });
    byId.get(s.id).times.push(s.t);
  }
  return [...byId.values()].filter((b) => b.times.length > 1);
}

// [rc4l] What the page's audio output carries, through an analyser spliced in front of each context's destination and sampled every 20 ms, so window.fuaLevel.mean() is its RMS since window.fuaLevel.reset(), peak() its loudest sample and contexts() each context's state and clock, and a check hears a sound rather than only seeing it start.
export const WATCH_LEVEL = `window.fuaLevel = {
    taps: [],
    reset() { for (const t of this.taps) { t.sum = 0; t.n = 0; t.max = 0; } },
    mean() { let s = 0, n = 0; for (const t of this.taps) { s += t.sum; n += t.n; } return n ? s / n : 0; },
    peak() { let m = 0; for (const t of this.taps) m = Math.max(m, t.max); return m; },
    contexts() { return this.taps.map((t) => ({ state: t.ctx.state, time: Number(t.ctx.currentTime.toFixed(2)) })); },
  }; {
    const connect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function (target, ...rest) {
      if (!(target instanceof AudioDestinationNode)) return connect.call(this, target, ...rest);
      let tap = window.fuaLevel.taps.find((t) => t.ctx === this.context);
      if (!tap) {
        const an = this.context.createAnalyser();
        an.fftSize = 2048;
        connect.call(an, target);
        tap = { ctx: this.context, an, buf: new Float32Array(2048), sum: 0, n: 0, max: 0 };
        window.fuaLevel.taps.push(tap);
        setInterval(() => { an.getFloatTimeDomainData(tap.buf); let s = 0; for (const v of tap.buf) s += v * v; const r = Math.sqrt(s / tap.buf.length); tap.sum += r; tap.n++; if (r > tap.max) tap.max = r; }, 20);
      }
      return connect.call(this, tap.an, ...rest);
    };
  }`;

// [rc4l] Each browser frame's time in window.fuaFrames, from requestAnimationFrame, for a run to judge the pacing a player sees.
export const WATCH_FRAMES = `window.fuaFrames = []; requestAnimationFrame(function tick(t) { window.fuaFrames.push(t); requestAnimationFrame(tick); });`;

// [rc4l] The gaps between frame times as one line: how many, their median, p99 and longest, and how many ran past 20 and 50 ms, the hitches a player feels.
export function framePacing(times) {
  const gaps = times.slice(1).map((t, i) => t - times[i]).sort((a, b) => a - b);
  if (!gaps.length) return 'frames: none drawn';
  const at = (p) => gaps[Math.min(gaps.length - 1, Math.floor(p * gaps.length))];
  return `frames: ${gaps.length}, interval median ${at(0.5).toFixed(2)} p99 ${at(0.99).toFixed(2)} max ${gaps[gaps.length - 1].toFixed(1)} ms, over 20 ms ${gaps.filter((g) => g > 20).length}, over 50 ms ${gaps.filter((g) => g > 50).length}`;
}
