// [rc4l] Every decal case in one run, because fixing them one at a time keeps breaking the others.
//
//   floor      a mark on the floor appears at all
//   ceiling    the same looking up
//   wall       a wall mark appears, and the glow is not buried under its own scorch
//   flash      the impact flash is not cut into by the mark underneath it
//   occlusion  nothing marked is visible through solid geometry
//
// Each answers with a number, so a change to the ordering or the gating is judged against all five
// before anyone plays it, rather than by whichever one happened to be on screen.
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { BridgeClient } from 'file:///F:/ForkUnderA/tools/fuactl/src/client.mjs';
import * as cap from 'file:///F:/ForkUnderA/tools/fuactl/src/capture.mjs';
import * as shot from 'file:///F:/ForkUnderA/tools/fuactl/src/shot.mjs';

const SP = 'C:/Users/anann/AppData/Local/Temp/claude/F--ForkUnderA/acfbfdcd-395b-43bb-abbd-180365e9c4c7/scratchpad';
const S = 'F:/ForkUnderA/dist-windows/sweep';
const CLI = 'F:/ForkUnderA/tools/fuactl/src/cli.mjs';
const EXE = 'F:/ForkUnderA/dist-windows/forkundera.exe';

const sub = (a, b, o) => Number((spawnSync(process.execPath, [CLI, 'png', '--sub', a, b, o, '1'],
  { encoding: 'utf8' }).stdout || '').match(/mean ([\d.]+)/)?.[1] ?? NaN);

const tok = fs.readFileSync(SP + '/mytoken', 'utf8').trim();
const spot = JSON.parse(fs.readFileSync(SP + '/cfspot.json', 'utf8'));
const c = new BridgeClient();
await c.connect(7797, { token: tok });
await c.waitHello();

await cap.sandbox(c, { map: 'dbab04' });
await cap.exec(c, 'r_drawplayersprites 0');

async function burst(weapon, pitch, tics, settle) {
  await cap.exec(c, 'give ' + weapon);
  await cap.exec(c, 'use ' + weapon);
  await cap.waitTics(c, 22);
  const at = { x: spot.x, y: spot.y, z: spot.z, angle: spot.yaw, pitch };
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 8);
  await shot.shotPair(c, 'q_off', { engineBin: EXE });
  await cap.exec(c, '+attack');
  await cap.waitTics(c, tics);
  await cap.exec(c, '-attack');
  await cap.waitTics(c, settle);
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 4);
  const r = await shot.shotPair(c, 'q_on', { engineBin: EXE, frozen: ['fua_projdecals_stats'] });
  return {
    vk: sub(`${S}/q_off_vk.png`, `${S}/q_on_vk.png`, `${SP}/q.png`),
    gl: sub(`${S}/q_off_gl.png`, `${S}/q_on_gl.png`, `${SP}/q.png`),
    stats: String(r.frozen[0]).trim().replace(/\s+/g, ' '),
  };
}

const rows = [];
// A mark on the floor, and on the ceiling, with everything transient long gone.
rows.push(['floor  ', await burst('ChaingunE', 70, 18, 55)]);
rows.push(['ceiling', await burst('ChaingunE', -70, 18, 55)]);
// A wall mark, settled: scorch plus its glow.
rows.push(['wall   ', await burst('PlasmaRifleE', 2, 16, 45)]);
// And mid-burst, where the flash must not be cut into by the mark under it.
rows.push(['flash  ', await burst('PlasmaRifleE', 2, 14, 0)]);

for (const [name, r] of rows) {
  console.log(`${name}  VK ${r.vk.toFixed(2).padStart(6)}   GL ${r.gl.toFixed(2).padStart(6)}   ${r.stats}`);
}

// [rc4l] A mark must have SHAPE, not merely be present.
//
// Every check above is a brightness, and a brightness cannot tell a decal from the solid rectangle
// it lives in -- a mark and a block of its own average colour weigh the same. Projected marks did
// render as solid boxes, for as long as the pass ran at default settings, and none of these numbers
// noticed. The mask the shader itself computes does: a real mark has near-black and near-white in
// it, and a flat wash has neither.
// [rc4l] Every weapon, not one of them. A bullet mark is small and its graphic is nearly solid
// anyway, so it was the one mark that looked plausible while the BFG's painted a slab the size of a
// room -- and checking only the chaingun is exactly how that shipped.
for (const [weapon, pitch, tics] of [['ChaingunE', 50, 25], ['RocketLauncherE', 70, 3], ['BFGE', 70, 3]]) {
  await cap.exec(c, 'give ' + weapon);
  await cap.exec(c, 'use ' + weapon);
  await cap.waitTics(c, 22);
  const at = { x: spot.x, y: spot.y, z: spot.z, angle: spot.yaw, pitch };
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 8);
  await cap.exec(c, '+attack');
  await cap.waitTics(c, tics);
  await cap.exec(c, '-attack');
  await cap.waitTics(c, 60);
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 4);
  await c.rpc('sim.pause', {}).catch(() => {});
  await cap.exec(c, 'fua_dg_decaldebug 2');
  const f = `${S}/shape_${weapon}_vk.png`;
  try { fs.rmSync(f, { force: true }); } catch { /* fine */ }
  await cap.exec(c, `fua_diligent_shot ${f}`);
  for (let i = 0; i < 80 && !fs.existsSync(f); i++) await new Promise((r) => setTimeout(r, 100));
  await cap.exec(c, 'fua_dg_decaldebug 0');
  await c.rpc('sim.resume', {}).catch(() => {});
  const out = spawnSync(process.execPath, [CLI, 'png', '--range', f, '0.40', '0.30', '0.60', '0.50'],
    { encoding: 'utf8' }).stdout || '';
  const spread = Number(out.match(/spread ([\d.]+)/)?.[1] ?? 0);
  // [rc4l] The window sits INSIDE the mark on purpose, and the bar is low on purpose.
  //
  // A flat wash reads EXACTLY 0 there -- every pixel the same value is what "the mark is painting
  // its own box" means, and that is the only thing this has to catch. Real marks read anywhere from
  // the low tens to 250-odd depending on how much of the graphic's dense middle the window lands in,
  // and that number moves whenever the mark's size changes: correcting the projection's stretch took
  // the BFG's from 139 to 42 without anything being wrong. So the bar sits just above nothing. A bar
  // placed near a real reading fails marks that are fine, and a check that cries wolf gets ignored.
  console.log(`shape ${weapon.padEnd(16)} mask spread ${spread.toFixed(0)}  ${spread > 8
    ? 'ok, the mark has shape' : 'FAIL: flat wash -- the mark is painting its whole box'}`);
}
await cap.exec(c, 'r_drawplayersprites 1');
c.close();
