// [rc4l] A map reset respawns a killed monster facing its map angle in whole degrees (zandronum@28e63eb130d7), where it rounded down to a multiple of 45.
//
// usage:  node scenarios/resetangle.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const STATUE = "ZXStatue", STATUE_ED = 15094;
// [rc4l] One angle between the compass points, one on them, each a statue's.
export const ANGLES = [30, 135];

// [rc4l] A monster that never moves or turns, so its angle after the reset is the one it spawned with.
export const DECORATE = `actor ${STATUE} : ZombieMan ${STATUE_ED}\n{\n  states\n  {\n  Spawn:\n    POSS A -1\n    stop\n  }\n}\n`;

export function buildResetAnglePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 384, y: 64, angle: 90, type: 1 });
  ANGLES.forEach((angle, i) => m.thing({ x: 192 + 384 * i, y: 384, angle, type: STATUE_ED }));
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: DECORATE },
  ]);
}

// [rc4l] What is wrong with the living statues' angles, nearest degree, against the map's.
export function judgeAngles(actors) {
  const statues = (actors || []).filter((a) => a.c === STATUE && a.health > 0).sort((a, b) => a.x - b.x);
  if (statues.length !== ANGLES.length) return [`${statues.length} living statues, not ${ANGLES.length}`];
  return statues.flatMap((s, i) => (Math.round(s.angle) % 360 === ANGLES[i] ? [] : [`the statue spawned at ${ANGLES[i]} faces ${s.angle.toFixed(1)}`]));
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  await cap.exec(c, "kill monsters");
  await cap.waitTics(c, 10);
  const dead = (await c.rpc("world.actors", { all: true }))?.actors?.filter((a) => a.c === STATUE && a.health > 0).length;
  if (dead !== 0) return { line: `FAIL ${label}: ${dead} statues outlived kill monsters`, failed: 1 };
  await cap.exec(c, "resetmap");
  await cap.waitTics(c, 10);
  const faults = judgeAngles((await c.rpc("world.actors", { all: true }))?.actors);
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the reset statues face ${ANGLES.join(" and ")}`, failed: 0 };
}

// [rc4l] Survival, a mode whose maps reset, with monsters, which launches leave out, set before the map loads.
export const SIDES = [{ name: "vk", launchCvars: { survival: 1, sv_nomonsters: 0 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "resetangle", wad: buildResetAnglePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
