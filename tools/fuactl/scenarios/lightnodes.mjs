// [rc4l] Hunt the light nodes that outlive their lights.
//
// GL lights a wall by walking side_t::lighthead. A light is a thinker with its own lifetime; its
// node is unlinked separately, and when the two disagree the surface stays lit by something that no
// longer exists. Every instrument that starts from the thinker list reports nothing at all --
// fua_dg_lights says "0 active" while the wall is visibly blue -- so the only way to see it is to
// walk the lists, which is what fua_lightnodes does.
//
// It was caught once by hand and then would not reproduce, which is the whole reason this exists: a
// fault that needs a light to have existed before a level change, and appears somewhere in the
// crossing, cannot be chased one manual attempt at a time. This drives the sequence over and over
// and reports the first step where the count leaves zero, with enough of the history to say what led
// there.
//
// usage:  node scenarios/lightnodes.mjs [cycles]
import fs from 'node:fs';
import { BridgeClient } from '../src/client.mjs';
import * as cap from '../src/capture.mjs';

const SP = 'C:/Users/anann/AppData/Local/Temp/claude/F--ForkUnderA/acfbfdcd-395b-43bb-abbd-180365e9c4c7/scratchpad';
const NL = String.fromCharCode(10);
const CYCLES = Number(process.argv[2]) || 6;

const tok = fs.readFileSync(SP + '/mytoken', 'utf8').trim();
const spot = JSON.parse(fs.readFileSync(SP + '/cfspot.json', 'utf8'));
const at = { x: spot.x, y: spot.y, z: spot.z, angle: spot.yaw, pitch: 2 };

const c = new BridgeClient();
await c.connect(7797, { token: tok });
await c.waitHello();

// Dead nodes, as one number. Everything below is "did this step move it off zero".
async function dead(label) {
  const out = String(await cap.exec(c, 'fua_lightnodes'));
  const m = out.match(/sides (\d+) linked \((\d+) orphaned, (\d+) dormant\), subsectors (\d+) linked \((\d+) orphaned\)/);
  const sideDead = m ? Number(m[2]) : -1;
  const subDead = m ? Number(m[5]) : -1;
  const dormant = m ? Number(m[3]) : -1;
  const bad = sideDead > 0 || subDead > 0;
  console.log(`  ${label.padEnd(38)} orphaned ${String(sideDead).padStart(3)}/${String(subDead).padStart(3)}  dormant ${String(dormant).padStart(3)}` +
    (bad ? '   <-- LEAKED' : ''));
  if (bad) {
    // The detail lines say which of the three states each node is in, and that is the thing that
    // decides where the fix goes: a node with no light behind it was orphaned by a destroy that
    // did not unlink, a zero-radius one by a sweep that did not run.
    for (const l of out.split(NL).filter((x) => /node with/.test(x)).slice(0, 4)) console.log('     ', l.trim());
  }
  return bad;
}

async function fire(slot, tics) {
  await cap.exec(c, 'slot ' + slot);
  await cap.waitTics(c, 20);
  await c.rpc('player.setpos', at);
  await cap.waitTics(c, 6);
  await cap.exec(c, '+attack');
  await cap.waitTics(c, tics);
  await cap.exec(c, '-attack');
  // Long enough for every light this made to have died on its own.
  await cap.waitTics(c, 180);
}

// [rc4l] `rearm` is not a convenience, it is a variable of the experiment.
//
// A `map` change puts the player back on starting weapons, so a run that gives them once at the top
// and then asks for slot 6 afterwards is firing a PISTOL, not a plasma rifle. The one run that ever
// caught this leak did exactly that without meaning to, and every tidied-up rerun that re-armed
// first came back clean -- so which weapon fired after the crossing is a live suspect rather than a
// detail to normalise away.
async function goto(map, rearm) {
  await cap.exec(c, 'map ' + map);
  await cap.waitTics(c, 90);
  await cap.exec(c, 'r_drawplayersprites 0');
  if (rearm) {
    for (const g of ['PlasmaRifle', 'Chaingun', 'RocketLauncher', 'BFG9000']) await cap.exec(c, 'give ' + g);
  }
  await cap.waitTics(c, 15);
}

await cap.sandbox(c, { map: 'dbab04' });
await cap.exec(c, 'r_drawplayersprites 0');
for (const g of ['PlasmaRifle', 'Chaingun', 'RocketLauncher', 'BFG9000']) await cap.exec(c, 'give ' + g);
await cap.waitTics(c, 15);

let found = false;
for (let cycle = 1; cycle <= CYCLES && !found; cycle++) {
  console.log(`cycle ${cycle}`);
  // The sequence that caught it once: fire armed, cross and come straight back WITHOUT re-arming,
  // and fire again with whatever the player now has.
  found = await dead('at the start') || found;
  await fire(6, 40);
  found = await dead('after plasma here') || found;
  await goto('dbab03', false);
  await goto('dbab04', false);
  found = await dead('after the round trip') || found;
  await fire(6, 40);
  found = await dead('after firing back here, unarmed') || found;
  await goto('dbab04', true);
  await fire(6, 40);
  found = await dead('and again, re-armed') || found;
}

console.log(found ? 'LEAK REPRODUCED' : `no leak in ${CYCLES} cycles`);
await cap.exec(c, 'r_drawplayersprites 1');
c.close();
