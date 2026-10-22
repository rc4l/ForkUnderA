// [rc4l] A gun switch that drops the floor the player stands on: exit 0 when the shot flips the switch, the floor drops and the backend patches what changed rather than rebuilding the whole scene, which cost a 57 ms frame on Sunder MAP16 (queue #57).
//
// usage:  node scenarios/gunswitch.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import * as cap from "../src/capture.mjs";

const TAG = 7;

// [rc4l] A hall whose north wall is the switch, a platform in it level with the hall floor, and a pit in the platform it lowers to, so the drop both retires the platform's walls and grows new ones.
export function buildGunSwitchWad() {
  const m = new UdmfMap();
  const hall = m.room(0, 0, 768, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 200 }, (edge) => (edge === "north"
    ? { side: { texturemiddle: "SW1BRCOM" }, line: { special: SPECIAL.Floor_LowerToLowest, arg0: TAG, arg1: 32, impact: true } }
    : { side: { texturemiddle: "STARTAN2" } }));
  const wall = () => ({ texturebottom: "STARTAN2", texturetop: "STARTAN2" });
  const platform = m.block(192, 96, 576, 320, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT1",
    textureceiling: "CEIL3_5", lightlevel: 200, id: TAG }, hall, wall, wall);
  m.block(320, 128, 448, 160, { heightfloor: -128, heightceiling: 256, texturefloor: "NUKAGE1",
    textureceiling: "CEIL3_5", lightlevel: 200 }, platform, wall, wall);
  m.thing({ x: 384, y: 240, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const SIDES = [{ name: "vk", noGl: true }];

const REBUILDS = /geometry: (\d+) scene rebuilds since load/;

async function rebuilds(c) {
  const m = REBUILDS.exec(await cap.exec(c, "fua_dg_dynstats", { quietMs: 400 }));
  return m ? Number(m[1]) : NaN;
}

// [rc4l] The shot's result as a line: the floor must have dropped, or the switch never fired and nothing was tested.
export function judgeGunSwitch(label, { before, after, floorBefore, floorAfter }) {
  if (!(floorAfter < floorBefore)) return { line: `FAIL ${label}: the floor stayed at ${floorAfter}, so the switch never fired`, failed: 1 };
  if (!(after <= before)) return { line: `FAIL ${label}: ${after - before} whole-scene rebuild(s) for one switch and a dropping floor`, failed: 1 };
  return { line: `PASS ${label}: the floor dropped ${floorBefore - floorAfter} with no whole-scene rebuild`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  await c.rpc("player.setpos", { x: 384, y: 240, z: 0, angle: 90, pitch: 0 });
  await cap.waitTics(c, 35);
  const before = await rebuilds(c);
  const floorBefore = (await c.rpc("player.camera")).floorz;
  await cap.exec(c, "+attack");
  await cap.waitTics(c, 6);
  await cap.exec(c, "-attack");
  await cap.waitTics(c, 140);
  const after = await rebuilds(c);
  const floorAfter = (await c.rpc("player.camera")).floorz;
  return judgeGunSwitch(label, { before, after, floorBefore, floorAfter });
}

runScenarioIfMain(import.meta.url, () => ({ name: "gunswitch", wad: buildGunSwitchWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
