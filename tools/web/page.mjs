// [rc4l] Code for a page, shared by the checks and the probe: the watches run before its own scripts, and what runs once the engine has started.
import { soakStep } from '../fuactl/src/telemetry.mjs';

// [rc4l] JavaScript run in the page once the engine has started (fua.js sets fuaCommand just before main), its value the caller's result.
export function inPage(body) {
  return `(async () => { while (!(window.Module && Module.fuaCommand)) await new Promise((r) => setTimeout(r, 100)); ${body} })()`;
}

// [rc4l] Page code that waits ms milliseconds.
export const pause = (ms) => `await new Promise((r) => setTimeout(r, ${ms}));`;

// [rc4l] Page code that keeps every console line from here on in said until unhear(), and lastJson(start, none) parses the newest one starting with start.
export const HEAR_CONSOLE = `const said = [];
      const print = console.log;
      console.log = (...a) => { said.push(a.join(' ')); return print.apply(console, a); };
      const unhear = () => { console.log = print; };
      const lastJson = (start, none) => { const line = said.slice().reverse().find((l) => l.startsWith(start)); return line ? JSON.parse(line) : none; };`;

// [rc4l] Page code that repeats a soak action (fuactl's SOAK_ACTIONS) cycles times on map, marking memory after each once a new level has printed its title and the page's garbage is collected (window.gc, with --expose-gc), and returns the engine's verdict.
export function soakInPage(action, cycles, settleMs, map = 'MAP01') {
  const step = soakStep(action, map);
  return inPage(`
      ${pause(2000)}
      ${HEAR_CONSOLE}
      const started = async (from) => { for (let t = 0; t < 600 && !said.slice(from).some((l) => l.trim().toLowerCase().startsWith(${JSON.stringify(`${map.toLowerCase()} - `)})); t++) ${pause(100)} };
      const collect = async () => { for (let k = 0; k < 2 && window.gc; k++) { window.gc(); ${pause(200)} } };
      Module.fuaCommand('fua_soak begin');
      for (let i = 0; i < ${cycles}; i++) {
        const from = said.length;
        Module.fuaCommand(${JSON.stringify(step.text)});
        ${step.newLevel ? 'await started(from);' : ''}
        ${pause(settleMs)}
        await collect();
        Module.fuaCommand('fua_soak mark');
      }
      Module.fuaCommand('fua_soak verdict 1 json');
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
