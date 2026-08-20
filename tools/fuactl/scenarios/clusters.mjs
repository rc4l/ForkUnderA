// [rc4l] Does the cluster grid draw the same picture as testing every light -- and is it faster?
//
// Clustering is an acceleration, not a look. So the first question is not "does it work" but "is it
// the same", and it is asked the only way that means anything: the same frozen frame rendered both
// ways and diffed. A cell boundary the binning pass and the shader disagree about is the one failure
// this design has, and it shows up as lighting that is subtly wrong in a band -- which is exactly
// the kind of thing that survives being looked at.
//
// Then the point of the phase: light count is supposed to stop mattering. The engine spawns a field
// of standing lights (fua_light, which holds still, unlike everything the game spawns) and the frame
// time is measured with the grid on and off at each count.
//
// usage:  node scenarios/clusters.mjs [lights]
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { BridgeClient } from 'file:///F:/ForkUnderA/tools/fuactl/src/client.mjs';
import * as cap from 'file:///F:/ForkUnderA/tools/fuactl/src/capture.mjs';
import * as shot from 'file:///F:/ForkUnderA/tools/fuactl/src/shot.mjs';

const SP = 'C:/Users/anann/AppData/Local/Temp/claude/F--ForkUnderA/acfbfdcd-395b-43bb-abbd-180365e9c4c7/scratchpad';
const S = 'F:/ForkUnderA/dist-windows/sweep';
const CLI = 'F:/ForkUnderA/tools/fuactl/src/cli.mjs';
const EXE = 'F:/ForkUnderA/dist-windows/forkundera.exe';
const COUNTS = (process.argv[2] || '0,1,8,64,256').split(',').map(Number);

const run = (...a) => (spawnSync(process.execPath, [CLI, 'png', ...a], { encoding: 'utf8' }).stdout || '').trim();

// [rc4l] Below the sky, because the sky moves whether or not anything is paused.
//
// Two shots of the same paused frame differ by 0.6%, all of it in the top rows -- the worst row is
// 63% different and it is the sky scrolling on the render clock rather than the sim clock. That is
// four times the size of a cluster fault worth finding, sitting in the same number. So the sky is
// cropped out before the diff, and what is left is the world, which really does hold still.
const WORLD = ['0.0', '0.28', '1.0', '1.0'];
function diffPct(a, b) {
  run('--crop', a, `${S}/cl_cmp_a.png`, ...WORLD, '1');
  run('--crop', b, `${S}/cl_cmp_b.png`, ...WORLD, '1');
  return Number(/differ\s+([\d.]+)%/.exec(run('--diff', `${S}/cl_cmp_a.png`, `${S}/cl_cmp_b.png`))?.[1] ?? NaN);
}

const sess = fs.readFileSync('F:/ForkUnderA/tools/fuactl/.play-session', 'utf8');
const spot = JSON.parse(fs.readFileSync(SP + '/cfspot.json', 'utf8'));
const at = { x: spot.x, y: spot.y, z: spot.z, angle: spot.yaw, pitch: 2 };

const c = new BridgeClient();
await c.connect(Number(/PORT=(\d+)/.exec(sess)[1]), { token: /TOKEN=(\w+)/.exec(sess)[1] });
await c.waitHello();

await cap.sandbox(c, { map: 'dbab04' });
await cap.exec(c, 'r_drawplayersprites 0');
await cap.waitTics(c, 15);

// [rc4l] Lights that HOLD STILL, scattered around the spot rather than piled on it.
//
// Everything the game spawns moves or expires, and a light in a different place in two captures
// makes every difference between them arguable. These are placed from the console at stated points,
// so the same field is there in both renders and at every count.
// [rc4l] Two distributions, because the first one flattered nothing and misled about why.
//
// 'packed' puts every light within 400 units of the camera, which is a stress test and not a map: a
// 96-unit light standing 100 units away genuinely does reach most of what you can see, so a grid
// cannot help and honestly reports that by giving it every cell. Measured at 256 lights it claimed
// 695 of 3840 cells EACH, which is not the grid failing -- it is the answer.
//
// 'spread' is what a map full of torches looks like: lights over the whole level, most of them
// behind you or too far to matter. That is the case clustering exists for, and the only one whose
// timing means anything.
async function field(n, spread) {
  await cap.exec(c, 'fua_light_clear');
  // One command, not one per light: at a thousand lights the round trips took longer than the
  // measurement, which is how a benchmark stops being run. The engine places the same golden-angle
  // spiral, and puts each light above the floor of the sector it actually lands in.
  if (n > 0) await cap.exec(c, `fua_light_field ${n} 96 ${spread ? 3000 : 400}`);
  await cap.waitTics(c, 8);
}

async function frameAt(tag, frozen = []) {
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 6);
  return shot.shotPair(c, tag, { engineBin: EXE, frozen });
}

// Frame time from the backend's own bench, which renders N frames back to back and reports the mean.
async function bench(n = 200) {
  const out = String(await cap.exec(c, `fua_diligent_bench ${n}`));
  const ms = /([\d.]+)\s*ms/.exec(out);
  return ms ? Number(ms[1]) : NaN;
}

// [rc4l] BOTH renders inside ONE frozen window, or the comparison measures the wrong thing.
//
// Capturing twice with a resume in between lets the world move: textures animate, the sky scrolls,
// the sim advances a tic. Done that way this reported ~2% difference with ZERO lights and clustering
// switched off entirely -- 2% of pure noise, sitting exactly where a real cluster fault would show
// up, and comfortably large enough to hide one. Pause once, render both ways, resume.
async function bothWays(tag) {
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 6);
  await c.rpc('sim.pause', {}).catch(() => {});
  await cap.exec(c, 'fua_dg_clusters 1');
  await cap.waitTics(c, 2);
  await cap.exec(c, `fua_diligent_shot ${S}/${tag}_on.png`);
  const stats = String(await cap.exec(c, 'fua_dg_dynstats'));
  await cap.exec(c, 'fua_dg_clusters 0');
  await cap.waitTics(c, 2);
  await cap.exec(c, `fua_diligent_shot ${S}/${tag}_off.png`);
  await cap.exec(c, 'fua_dg_clusters 1');
  await c.rpc('sim.resume', {}).catch(() => {});
  return stats;
}

console.log('same picture, both ways (one frozen frame)');
for (const n of COUNTS) {
  await field(n, false);
  const statsText = await bothWays('cl');
  const on = { frozen: [statsText] };
  const d = diffPct(`${S}/cl_on.png`, `${S}/cl_off.png`);
  const stats = /clusters: (\d+) cells, (\d+) light refs[^|]*/.exec(String(on.frozen[0] || ''));
  console.log(`  ${String(n).padStart(4)} lights: differ ${d.toFixed(2)}%   ${stats ? stats[0].trim() : 'no cluster stats'}`);
  if (d > 0.05) {
    fs.copyFileSync(`${S}/cl_on.png`, `${SP}/cluster_mismatch_${n}_on.png`);
    fs.copyFileSync(`${S}/cl_off.png`, `${SP}/cluster_mismatch_${n}_off.png`);
    console.log(`     kept the pair: ${SP}/cluster_mismatch_${n}_{on,off}.png`);
  }
}

for (const spread of [false, true]) {
  console.log('');
  console.log(`does light count still cost anything -- ${spread ? 'spread over the map' : 'packed around the camera'}`);
  for (const n of COUNTS) {
    await field(n, spread);
    await c.rpc('player.setpos', at);
    await cap.waitTics(c, 6);
    await cap.exec(c, 'fua_dg_clusters 1');
    const on = await bench();
    const refs = /clusters: (\d+) cells, (\d+) light refs/.exec(String(await cap.exec(c, 'fua_dg_dynstats')));
    await cap.exec(c, 'fua_dg_clusters 0');
    const off = await bench();
    console.log(`  ${String(n).padStart(4)} lights: clustered ${on.toFixed(3)} ms   every light ${off.toFixed(3)} ms` +
      (refs ? `   (${refs[2]} refs, ${(Number(refs[2]) / Math.max(n, 1)).toFixed(0)} cells per light)` : ''));
  }
}

await cap.exec(c, 'fua_light_clear');
await cap.exec(c, 'fua_dg_clusters 1');
await cap.exec(c, 'r_drawplayersprites 1');
c.close();
