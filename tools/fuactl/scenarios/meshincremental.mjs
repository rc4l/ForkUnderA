// [rc4l] Does the incrementally-maintained mesh draw what a rebuilt one would?
//
// The backend keeps its vertex buffer up to date by patching what moved, appending what appeared and
// collapsing what the world dropped -- instead of sorting and re-uploading 800,000 vertices whenever
// anything changed. That is worth ~4.5 ms a frame on Sunder MAP16 and it is exactly the kind of
// bookkeeping that goes subtly wrong: a stale slot still drawn, an appended piece in the wrong batch,
// a retired one left visible.
//
// So the test is the same one used for clustered lighting: the picture, both ways, in one frozen
// frame. Walk somewhere new (which forces appends as unseen segs bake), then force a full rebuild and
// compare. Any difference is the incremental path disagreeing with the thing it is standing in for.
//
// usage:  node scenarios/meshincremental.mjs [map] [stops]
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { BridgeClient } from 'file:///F:/ForkUnderA/tools/fuactl/src/client.mjs';
import * as cap from 'file:///F:/ForkUnderA/tools/fuactl/src/capture.mjs';

const MAP = process.argv[2] || 'map16';
const STOPS = Number(process.argv[3]) || 6;
const S = 'F:/ForkUnderA/dist-windows/sweep';
const CLI = 'F:/ForkUnderA/tools/fuactl/src/cli.mjs';
const run = (...a) => (spawnSync(process.execPath, [CLI, 'png', ...a], { encoding: 'utf8' }).stdout || '').trim();
// [rc4l] The WORLD, not the console text over it.
//
// The first run of this reported 19.8% and the pair looked identical: the difference was the stats
// line this script itself had printed, still drawn across the top of one shot and not the other.
// Same mistake as measuring a scrolling sky. Crop to the middle band and the text and the status bar
// are out of it.
const WORLD = ['0.0', '0.14', '1.0', '0.9'];
function diffPct(a, b) {
  run('--crop', a, `${S}/inc_cmp_a.png`, ...WORLD, '1');
  run('--crop', b, `${S}/inc_cmp_b.png`, ...WORLD, '1');
  return Number(/differ\s+([\d.]+)%/.exec(run('--diff', `${S}/inc_cmp_a.png`, `${S}/inc_cmp_b.png`))?.[1] ?? NaN);
}

const sess = fs.readFileSync('F:/ForkUnderA/tools/fuactl/.play-session', 'utf8');
const c = new BridgeClient();
await c.connect(Number(/PORT=(\d+)/.exec(sess)[1]), { token: /TOKEN=(\w+)/.exec(sess)[1] });
await c.waitHello();

await cap.exec(c, 'map ' + MAP);
await cap.waitTics(c, 150);
await cap.exec(c, 'god');
await cap.exec(c, 'r_drawplayersprites 0');
await cap.exec(c, 'notarget');

const stats = async () => {
  const t = String(await cap.exec(c, 'fua_dg_dynstats'));
  const m = /(\d+) geometry patches \((\d+) verts moved\), (\d+) appends/.exec(t) || [];
  const r = /geometry: (\d+) scene rebuilds/.exec(t) || [];
  return { patches: Number(m[1] || 0), appends: Number(m[3] || 0), rebuilds: Number(r[1] || 0) };
};

// [rc4l] Somewhere the player has not been, so segs bake and pieces are APPENDED rather than patched.
// Sunder's maps are enormous, so a spiral out from the spawn reaches unseen geometry quickly.
const spawn = await c.rpc('player.getpos', {}).catch(() => null);
const base = spawn && spawn.x !== undefined ? spawn : { x: 0, y: 0, z: 0 };

let worst = 0;
for (let i = 0; i < STOPS; i++) {
  const a = i * 2.39996;
  const r = 400 + 900 * i;
  await c.rpc('player.setpos', {
    x: base.x + r * Math.cos(a), y: base.y + r * Math.sin(a), z: base.z,
    angle: (i * 61) % 360, pitch: 0, force: 1,
  }).catch(() => {});
  await cap.waitTics(c, 30);

  const before = await stats();
  // Anything printed now lands on screen and lands in the comparison. Nothing is asked between the
  // two shots for that reason.
  await cap.waitTics(c, 12);
  await c.rpc('sim.pause', {}).catch(() => {});
  await cap.exec(c, `fua_diligent_shot ${S}/inc_a.png`);

  // Force the thing the incremental path is standing in for: invalidate and rebuild from scratch.
  await cap.exec(c, 'gl_wallmesh 0');
  await cap.waitTics(c, 4);
  await cap.exec(c, 'gl_wallmesh 1');
  await cap.waitTics(c, 8);
  await cap.exec(c, `fua_diligent_shot ${S}/inc_b.png`);
  await c.rpc('sim.resume', {}).catch(() => {});

  // [rc4l] The control: the same wait, the same two shots, WITHOUT rebuilding anything.
  //
  // Whatever moves on its own in that window -- animated textures re-resolving on the render clock,
  // a flickering sector -- moves in the real comparison too, and would be read as the incremental
  // path disagreeing with the rebuild. Measured here so it can be subtracted rather than argued
  // about; the first version of this test had no control and called 19% a failure.
  await c.rpc('sim.pause', {}).catch(() => {});
  await cap.exec(c, `fua_diligent_shot ${S}/inc_c.png`);
  await cap.waitTics(c, 12);
  await cap.exec(c, `fua_diligent_shot ${S}/inc_d.png`);
  await c.rpc('sim.resume', {}).catch(() => {});
  const control = diffPct(`${S}/inc_c.png`, `${S}/inc_d.png`);

  const d = diffPct(`${S}/inc_a.png`, `${S}/inc_b.png`);
  const after = await stats();
  worst = Math.max(worst, d - control);
  console.log(`stop ${i + 1}: incremental vs rebuilt ${d.toFixed(2)}%  (control ${control.toFixed(2)}%)` +
    `   (+${after.patches - before.patches} patches, +${after.appends - before.appends} appends,` +
    ` ${after.rebuilds} rebuilds total)`);
  if (d > Math.max(1.0, control * 1.5)) {
    fs.copyFileSync(`${S}/inc_a.png`, `${S}/inc_bad_${i}_incremental.png`);
    fs.copyFileSync(`${S}/inc_b.png`, `${S}/inc_bad_${i}_rebuilt.png`);
    console.log(`   kept the pair: sweep/inc_bad_${i}_{incremental,rebuilt}.png`);
  }
}
console.log(worst <= 1.0 ? `agrees everywhere (worst ${worst.toFixed(2)}% over control)` : `DISAGREES, worst ${worst.toFixed(2)}% over control`);
c.close();
