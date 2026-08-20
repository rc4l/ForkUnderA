// [rc4l] Does firing plasma leave a blue cast on the wall -- on the WALL, not on the marks?
//
// The report is that after plasma the room stays blue in GL and does not in Vulkan, and that firing
// something else clears it. Every measurement of it so far averaged a region the scorches are
// sitting in, which measures how much of the region is scorch: black pulls a warm wall toward
// neutral, so the number moved whenever marks landed or a map change cleared them, and read exactly
// like a cast that comes and goes.
//
// So this captures the same camera before and after and hands the pair to `png --tint`, which counts
// a pixel only where NEITHER frame has painted on it -- bright in both (not a scorch) and not much
// brighter in one (not a glow, a flash or a projectile). What is left is the wall itself, twice, and
// the difference between its b-r is the cast, if there is one.
//
// Repeated, because the report is that it comes and goes -- "the light lingers right as it is about
// to die". One clean round proves nothing about an intermittent fault, and the first run of this
// caught it at +3.36 while the second saw nothing. Every round says what the LIGHTS were doing in
// the same frozen frame as the capture, so a round that reproduces arrives with its own evidence.
//
// usage:  node scenarios/plasmatint.mjs [rounds] [bursttics]
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { BridgeClient } from 'file:///F:/ForkUnderA/tools/fuactl/src/client.mjs';
import * as cap from 'file:///F:/ForkUnderA/tools/fuactl/src/capture.mjs';
import * as shot from 'file:///F:/ForkUnderA/tools/fuactl/src/shot.mjs';

const SP = 'C:/Users/anann/AppData/Local/Temp/claude/F--ForkUnderA/acfbfdcd-395b-43bb-abbd-180365e9c4c7/scratchpad';
const S = 'F:/ForkUnderA/dist-windows/sweep';
const CLI = 'F:/ForkUnderA/tools/fuactl/src/cli.mjs';
const EXE = 'F:/ForkUnderA/dist-windows/forkundera.exe';
const ROUNDS = Number(process.argv[2]) || 4;
const BURST = Number(process.argv[3]) || 35;
const HIT = 1.0;   // bigger than the frame-to-frame noise, which measures at 0.00

// The wall the marks land on, and nothing else: no floor, no ceiling, no status bar.
const WALL = ['0.28', '0.34', '0.72', '0.62'];

function tint(a, b) {
  const out = spawnSync(process.execPath, [CLI, 'png', '--tint', a, b, ...WALL], { encoding: 'utf8' });
  const text = (out.stdout || '') + (out.stderr || '');
  return {
    cast: Number(/cast b-a: (-?[\d.]+)/.exec(text)?.[1] ?? NaN),
    px: Number(/surface (\d+)\//.exec(text)?.[1] ?? 0),
    text: text.trim(),
  };
}
const show = (v) => `${v >= 0 ? '+' : ''}${v.toFixed(2)}`;

const sess = fs.readFileSync('F:/ForkUnderA/tools/fuactl/.play-session', 'utf8');
const spot = JSON.parse(fs.readFileSync(SP + '/cfspot.json', 'utf8'));
const at = { x: spot.x, y: spot.y, z: spot.z, angle: spot.yaw, pitch: 2 };

const c = new BridgeClient();
await c.connect(Number(/PORT=(\d+)/.exec(sess)[1]), { token: /TOKEN=(\w+)/.exec(sess)[1] });
await c.waitHello();

async function frame(tag, frozen = []) {
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 6);
  return shot.shotPair(c, tag, { engineBin: EXE, frozen });
}

async function burst(slot, tics, settle) {
  await cap.exec(c, 'slot ' + slot);
  await cap.waitTics(c, 22);
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 6);
  await cap.exec(c, '+attack');
  await cap.waitTics(c, tics);
  await cap.exec(c, '-attack');
  // Long enough that every light the burst made has died on its own. A cast still here is the thing
  // being reported; one that fades out by itself is just the shot.
  await cap.waitTics(c, settle);
}

const log = [];
let caught = 0;
for (let round = 1; round <= ROUNDS; round++) {
  await cap.sandbox(c, { map: 'dbab04' });
  await cap.exec(c, 'r_drawplayersprites 0');
  for (const g of ['PlasmaRifle', 'Shotgun']) await cap.exec(c, 'give ' + g);
  await cap.waitTics(c, 15);

  await frame('tint_before');
  await burst(6, BURST, 200);
  const after = await frame('tint_plasma', ['fua_dg_lights', 'fua_lightnodes']);

  const r = {};
  for (const which of ['gl', 'vk']) r[which] = tint(`${S}/tint_before_${which}.png`, `${S}/tint_plasma_${which}.png`);
  const hit = Math.abs(r.gl.cast) >= HIT || Math.abs(r.vk.cast) >= HIT;
  console.log(`round ${round}: GL ${show(r.gl.cast)}  VK ${show(r.vk.cast)}` +
    `   (surface ${r.gl.px}/${r.vk.px} px)${hit ? '   <-- CAST' : ''}`);

  const entry = { round, gl: r.gl.cast, vk: r.vk.cast, frozen: after.frozen };
  if (hit) {
    caught++;
    // The lights in the SAME frozen frame as the capture -- the only ones that can be responsible.
    const lights = String(after.frozen[0] || '').split(String.fromCharCode(10)).slice(0, 6);
    for (const l of lights) if (l.trim()) console.log('    ' + l.trim());
    console.log('    ' + String(after.frozen[1] || '').trim());

    // Does it stay? And does firing something else clear it, as the report says?
    await cap.waitTics(c, 400);
    await frame('tint_later');
    for (const which of ['gl', 'vk']) console.log(`    still there after 400 tics, ${which.toUpperCase()}: ` +
      show(tint(`${S}/tint_before_${which}.png`, `${S}/tint_later_${which}.png`).cast));
    await burst(3, 10, 120);
    await frame('tint_cleared');
    for (const which of ['gl', 'vk']) console.log(`    after firing a shotgun, ${which.toUpperCase()}: ` +
      show(tint(`${S}/tint_before_${which}.png`, `${S}/tint_cleared_${which}.png`).cast));
    for (const f of ['tint_before', 'tint_plasma', 'tint_later', 'tint_cleared'])
      for (const which of ['gl', 'vk'])
        fs.copyFileSync(`${S}/${f}_${which}.png`, `${SP}/caught${round}_${f}_${which}.png`);
  }
  log.push(entry);
}

console.log(caught ? `reproduced in ${caught}/${ROUNDS} rounds` : `no cast in ${ROUNDS} rounds`);
fs.writeFileSync(SP + '/plasmatint.json', JSON.stringify(log, null, 2));
await cap.exec(c, 'r_drawplayersprites 1');
c.close();
