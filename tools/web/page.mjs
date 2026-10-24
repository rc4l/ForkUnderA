// [rc4l] Code for a page, shared by the checks and the probe: the watches run before its own scripts, and what runs once the engine has started.
import { soakSteps } from '../fuactl/src/telemetry.mjs';

// [rc4l] JavaScript run in the page once the engine has started (fua.js sets fuaCommand just before main), its value the caller's result.
export function inPage(body) {
  return `(async () => { while (!(window.Module && Module.fuaCommand)) await new Promise((r) => setTimeout(r, 100)); ${body} })()`;
}

// [rc4l] Page code that waits until the engine answers commands or the page shows its error, and returns which, with whether the page is cross-origin isolated.
export const STARTED = `(async () => {
  for (;;) {
    const panel = document.getElementById('error');
    if (panel && !panel.hidden) return { isolated: crossOriginIsolated, error: document.getElementById('error-text').textContent };
    if (window.Module && Module.fuaCommand) return { isolated: crossOriginIsolated, error: null };
    await new Promise((r) => setTimeout(r, 100));
  }
})()`;

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
export function soakInPage(action, cycles, settleMs, map = 'MAP01', warmup = 1, { heap = false } = {}) {
  return inPage(`
      ${pause(2000)}
      ${HEAR_CONSOLE}
      const started = async (from, title) => { for (let t = 0; t < 600 && !said.slice(from).some((l) => l.trim().toLowerCase().startsWith(title)); t++) ${pause(100)} };
      // [rc4l] The driver collects as a heap snapshot does when it can (check: gc), since the page's gc() left about 800 KB a level reload a full collection takes, and the soak read it as growth (queue #69); else the page's own.
      let gcs = 0;
      const collect = async () => {
        const n = ++gcs;
        console.log('check: gc ' + n);
        for (let t = 0; t < 30 && window.fuaGcTaken !== n; t++) ${pause(100)}
        if (window.fuaGcTaken !== n) for (let k = 0; k < 2 && window.gc; k++) { window.gc(); ${pause(200)} }
      };
      // [rc4l] A mark asked for during a wipe is taken once it ends, so each waits for its line.
      const marked = async (n) => { for (let t = 0; t < 1200 && !said.some((l) => l.trim() === 'telemetry: soak mark ' + n); t++) ${pause(100)} };
      // [rc4l] A heap snapshot the driver takes (probe.mjs --heap), waited for since taking one stops the page.
      const heapAt = async (label) => { console.log('check: heap ' + label); for (let t = 0; t < 1200 && window.fuaHeapTaken !== label; t++) ${pause(100)} };
      const steps = ${JSON.stringify(soakSteps(action, map))};
      // [rc4l] The page's WebGPU objects by label at the start and the end, so a failing soak shows which ones it kept.
      const held = (when) => console.log('soak gpu ' + when + ': ' + (Module.fuaGpuLedger ? Module.fuaGpuLedger.tagText().replace(/\\n/g, '; ') : 'no ledger'));
      // [rc4l] And by label as numbers, taken at the first mark the verdict judges and again at the end, so the verdict names what grew.
      const ledger = () => { const m = {}; for (const l of (Module.fuaGpuLedger ? Module.fuaGpuLedger.tagText() : '').split('\\n')) { const [tag, b] = l.split('\\t'); if (b !== undefined) m[tag || '(unnamed)'] = Number(b); } return m; };
      let base = ledger();
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
        await marked(i + 1);
        if (i === ${warmup}) { base = ledger(); ${heap ? "await heapAt('base');" : ''} }
      }
      ${heap ? "await heapAt('end');" : ''}
      held('end');
      const end = ledger();
      const kept = Object.keys(end).filter((t) => end[t] > (base[t] || 0) + 65536).map((t) => t + ' +' + Math.round((end[t] - (base[t] || 0)) / 1024) + ' KB');
      Module.fuaCommand('fua_soak verdict ${warmup} json');
      unhear();
      return Object.assign(lastJson('{"pass"', {}), { kept });`);
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

// [rc4l] A stream's pieces (the music's, 71 ms each) are shorter than this, so a buffer at least this long is a sound.
export const PIECE_S = 0.1;

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

// [rc4l] The longest wait between two browser frames, its length and its start, in ms from the first frame; 0 long with fewer than two.
export function longestGap(times) {
  let ms = 0, at = 0;
  for (let i = 1; i < times.length; i++) if (times[i] - times[i - 1] > ms) { ms = times[i] - times[i - 1]; at = times[i - 1] - times[0]; }
  return { ms, at };
}

// [rc4l] The page's WebGPU calls by kind, in window.fuaGpuCalls: bind groups by the layout they are for, bytes written to each buffer and copied into each, so a run sees what churns (queue #48 was the decal pass's 13,000 bind groups a second).
export const WATCH_GPU_CALLS = `window.fuaGpuCalls = { from: performance.now(), by: {} }; if (window.GPUDevice) {
  const add = (k, n) => { const by = window.fuaGpuCalls.by; const e = by[k] || (by[k] = { calls: 0, bytes: 0 }); e.calls++; e.bytes += n || 0; };
  const bind = GPUDevice.prototype.createBindGroup, write = GPUQueue.prototype.writeBuffer, copy = GPUCommandEncoder.prototype.copyBufferToBuffer;
  GPUDevice.prototype.createBindGroup = function (d) { add('bind group ' + ((d.layout && d.layout.label) || '?'), 0); return bind.call(this, d); };
  GPUQueue.prototype.writeBuffer = function (b, o, data, ...rest) { add('write ' + (b.label || '?'), rest[1] ?? data.byteLength); return write.call(this, b, o, data, ...rest); };
  GPUCommandEncoder.prototype.copyBufferToBuffer = function (s, so, d, doff, n) { add('copy to ' + (d.label || '?'), n); return copy.call(this, s, so, d, doff, n); };
}`;

// [rc4l] The calls since from, at nowMs, as one line of the busiest kinds, each a second's calls and megabytes.
export function gpuCallRates({ from, by }, nowMs, top = 8) {
  const seconds = Math.max(0.001, (nowMs - from) / 1000);
  const kinds = Object.entries(by).sort((a, b) => b[1].calls - a[1].calls || b[1].bytes - a[1].bytes).slice(0, top);
  if (!kinds.length) return 'gpu calls: none';
  return 'gpu calls a second: ' + kinds.map(([k, e]) => `${k} ${(e.calls / seconds).toFixed(0)}${e.bytes ? ` (${(e.bytes / seconds / 1048576).toFixed(2)} MB)` : ''}`).join(' | ');
}

// [rc4l] Page code defining grabFrame(), the next frame the engine draws as a base64 PNG, taken from the canvas just after it as the page's screenshot is, with the notify lines cleared first and no capture message (screenshot_quiet is left off).
export const GRAB_FRAME = `const grabFrame = () => new Promise((resolve) => {
        const canvas = Module.canvas;
        canvas.toBlob = (_, type) => {
          delete canvas.toBlob;
          canvas.toBlob(async (blob) => {
            Module.fuaCommand('screenshot_quiet 0');
            const bytes = new Uint8Array(await blob.arrayBuffer());
            let text = '';
            for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
            resolve(btoa(text));
          }, type);
        };
        Module.fuaCommand('clear');
        Module.fuaCommand('screenshot_quiet 1');
        Module.fuaCommand('screenshot');
      });`;

// [rc4l] Page code, for an init, that adds name to the files the page fetches through data/files.json and serves it from bytes, so a check can carry a map of its own without staging one.
export function serveData(name, bytes) {
  return `{
      const name = ${JSON.stringify(name)};
      const bytes = Uint8Array.from(atob(${JSON.stringify(Buffer.from(bytes).toString('base64'))}), (c) => c.charCodeAt(0));
      const real = window.fetch.bind(window);
      window.fetch = async (url, options) => {
        const at = String(url);
        if (at.endsWith('data/files.json')) return new Response(JSON.stringify([...(await (await real(url, options)).json()), { name, size: bytes.length }]));
        if (at.endsWith('data/' + name)) return new Response(bytes);
        return real(url, options);
      };
    }`;
}

// [rc4l] Page code, for an init, that replaces every find with replace in each WGSL shader the page makes, to try a shader change in a browser without building again.
export function swapWgsl(find, replace) {
  return `{
      const real = GPUDevice.prototype.createShaderModule;
      GPUDevice.prototype.createShaderModule = function (desc) {
        if (desc && typeof desc.code === 'string') desc = { ...desc, code: desc.code.split(${JSON.stringify(find)}).join(${JSON.stringify(replace)}) };
        return real.call(this, desc);
      };
    }`;
}
