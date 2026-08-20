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
await cap.exec(c, 'stat rendertimes');
await cap.waitTics(c, FRAMES);

const text = String(await cap.exec(c, 'fua_rendertimes'));
console.log(`--- ${MAP}, ${FRAMES} tics of warm clocks ---`);
console.log(text.trim());

const num = (re) => Number(re.exec(text)?.[1] ?? NaN);
const wallRender = num(/W: Render=([\d.]+)/);
const wallSetup = num(/Setup=([\d.]+), Clip/);
const wallClip = num(/Clip=([\d.]+)/);
const flatRender = num(/F: Render=([\d.]+)/);
const flatSetup = num(/F: Render=[\d.]+, Setup=([\d.]+)/);
const sprRender = num(/S: Render=([\d.]+)/);
const all = num(/All=([\d.]+)/);
const bsp = num(/BSP = ([\d.]+)/);
const draws = num(/Drawcalls=([\d.]+)/);
const finish = num(/Finish=([\d.]+)/);

const rows = [
  ['walls: render', wallRender], ['walls: setup', wallSetup], ['walls: clip', wallClip],
  ['flats: render', flatRender], ['flats: setup', flatSetup],
  ['sprites: render', sprRender],
  ['bsp walk', bsp], ['draw calls', draws], ['finish', finish],
];
console.log('');
console.log(`share of the ${all.toFixed(3)} ms frame:`);
for (const [name, ms] of rows.sort((a, b) => b[1] - a[1])) {
  if (!Number.isFinite(ms)) continue;
  const pct = all > 0 ? (100 * ms / all) : 0;
  console.log(`  ${name.padEnd(16)} ${ms.toFixed(3)} ms  ${'#'.repeat(Math.round(pct / 2)).padEnd(50)} ${pct.toFixed(1)}%`);
}

await cap.exec(c, 'stat rendertimes');
c.close();
