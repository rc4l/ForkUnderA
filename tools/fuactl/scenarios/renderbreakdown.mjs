// [rc4l] Where the 4 ms actually goes.
//
// Every plan in this repo quotes one number for the CPU cost of a frame and none of them split it.
// A phase aimed at that number can be aimed at the wrong half of it just as easily as the right one:
// a render graph does nothing for a BSP walk, and GPU culling does nothing for wall construction.
// The renderer has carried these clocks all along; nothing was reading them.
//
// Reports the split at a stated camera, with the clocks warm, on both renderers -- because "GL spends
// it here" and "the backend spends it there" are different facts and the port needs both.
//
// usage:  node scenarios/renderbreakdown.mjs [map] [frames]
import fs from 'node:fs';
import { BridgeClient } from 'file:///F:/ForkUnderA/tools/fuactl/src/client.mjs';
import * as cap from 'file:///F:/ForkUnderA/tools/fuactl/src/capture.mjs';

const MAP = process.argv[2] || 'map10';
const FRAMES = Number(process.argv[3]) || 200;

const sess = fs.readFileSync('F:/ForkUnderA/tools/fuactl/.play-session', 'utf8');
const c = new BridgeClient();
await c.connect(Number(/PORT=(\d+)/.exec(sess)[1]), { token: /TOKEN=(\w+)/.exec(sess)[1] });
await c.waitHello();

await cap.exec(c, 'map ' + MAP);
await cap.waitTics(c, 120);
await cap.exec(c, 'god');
await cap.exec(c, 'r_drawplayersprites 0');

// [rc4l] The clocks are free when nothing is measuring, which is why they have to be switched on --
// and why the first frames after switching them on are the only ones worth reading.
// [rc4l] The clocks report ONE FRAME, and one frame is not a measurement.
//
// Read once, the same map measured 11.7 ms and 15.1 ms twenty minutes apart -- a spread wider than
// anything this phase is trying to win, sitting in the number every decision would be made against.
// So: stand in one stated place, sample many frames, and report the median with the spread beside
// it, so a change smaller than the noise is visibly smaller than the noise.
await cap.exec(c, 'stat rendertimes');
await cap.waitTics(c, 40);

const SAMPLES = 15;
const keys = {
  all: /All=([\d.]+)/, wallRender: /W: Render=([\d.]+)/, wallSetup: /, Setup=([\d.]+), Clip/,
  wallClip: /Clip=([\d.]+)/, flatRender: /F: Render=([\d.]+)/, flatSetup: /F: Render=[\d.]+, Setup=([\d.]+)/,
  sprRender: /S: Render=([\d.]+)/, bsp: /BSP = ([\d.]+)/, draws: /Drawcalls=([\d.]+)/, finish: /Finish=([\d.]+)/,
};
const samples = {};
for (const k of Object.keys(keys)) samples[k] = [];
let counts = '';
for (let i = 0; i < SAMPLES; i++) {
  await cap.waitTics(c, 12);
  const t = String(await cap.exec(c, 'fua_rendertimes'));
  if (!counts) counts = (t.split(String.fromCharCode(10))[0] || '').trim();
  for (const [k, re] of Object.entries(keys)) {
    const v = Number(re.exec(t)?.[1] ?? NaN);
    if (Number.isFinite(v)) samples[k].push(v);
  }
}
const med = (a) => { const s = [...a].sort((x, y) => x - y); return s.length ? s[Math.floor(s.length / 2)] : NaN; };
const spread = (a) => (a.length ? Math.max(...a) - Math.min(...a) : NaN);

const all = med(samples.all);
console.log(`--- ${MAP}, median of ${SAMPLES} samples ---`);
console.log(counts);
console.log(`frame ${all.toFixed(3)} ms  (spread ${spread(samples.all).toFixed(3)} across samples)`);
console.log('');
const rows = [
  ['walls: render', 'wallRender'], ['walls: setup', 'wallSetup'], ['walls: clip', 'wallClip'],
  ['flats: render', 'flatRender'], ['flats: setup', 'flatSetup'], ['sprites: render', 'sprRender'],
  ['bsp walk', 'bsp'], ['draw calls', 'draws'], ['finish', 'finish'],
];
for (const [name, key] of rows.map((r) => r).sort((a, b) => med(samples[b[1]]) - med(samples[a[1]]))) {
  const m = med(samples[key]);
  if (!Number.isFinite(m)) continue;
  const pct = all > 0 ? (100 * m / all) : 0;
  console.log(`  ${name.padEnd(16)} ${m.toFixed(3)} ms +-${(spread(samples[key]) / 2).toFixed(3)}  ` +
    `${'#'.repeat(Math.round(pct / 2)).padEnd(50)} ${pct.toFixed(1)}%`);
}

await cap.exec(c, 'stat rendertimes');
c.close();
