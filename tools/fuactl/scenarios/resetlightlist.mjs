// [rc4l] A map reset puts a 3D-floored room's light list back with its planes (zandronum@0f08c6c87089): exit 0 when the list starts at the room's map ceiling and holds its 3D floor's map top again, where the reset kept the lowered heights.
//
// usage:  node scenarios/resetlightlist.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acsScripts, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

export const ROOM_TAG = 5, FLOOR3D_TAG = 6;
// [rc4l] The room's ceiling and its 3D floor's top as the map has them, and how far the script lowers each.
export const MAP_LIGHTS = [256, 96], LOWER = [64, 16];
// [rc4l] The script the map runs before the reset: the room's ceiling and its 3D floor's top both lowered.
export const LOWER_SCRIPT = [...callSpecial(SPECIAL.Ceiling_LowerByValue, [ROOM_TAG, 255, LOWER[0]]), ...callSpecial(SPECIAL.Ceiling_LowerByValue, [FLOOR3D_TAG, 255, LOWER[1]]), PCD.TERMINATE];

export function buildResetLightListPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: MAP_LIGHTS[0], texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192, id: ROOM_TAG }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 32, angle: 90, type: 1 });
  m.room(640, 0, 704, 64, { heightfloor: 64, heightceiling: MAP_LIGHTS[1], texturefloor: "FLAT14", textureceiling: "FLAT14", lightlevel: 96, id: FLOOR3D_TAG }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: ROOM_TAG, arg1: 1, arg2: 0, arg3: 255 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: LOWER_SCRIPT, open: false, number: 1 }], []) })) }]);
}

// [rc4l] What is wrong with the room's light list against the heights wanted, nearest half unit.
export function judgeLights(row, want, when) {
  const got = row?.lights || [];
  return got.length === want.length && got.every((h, i) => Math.abs(h - want[i]) <= 0.5) ? [] : [`${when} the room's light list is ${got.join(",") || "empty"}, not ${want.join(",")}`];
}

export async function judgeLaunch(c, inst, label) {
  const room = async () => (await c.rpc("world.sectorstate", { first: 0, count: 1 }))?.sectors?.[0];
  await cap.waitTics(c, 10);
  await cap.exec(c, "puke 1");
  await cap.waitTics(c, 70);
  const lowered = judgeLights(await room(), MAP_LIGHTS.map((h, i) => h - LOWER[i]), "lowered,");
  await cap.exec(c, "resetmap");
  await cap.waitTics(c, 10);
  const faults = [...lowered, ...judgeLights(await room(), MAP_LIGHTS, "after the reset")];
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the reset room's light list is back at ${MAP_LIGHTS.join(",")}`, failed: 0 };
}

// [rc4l] Survival, a mode whose maps reset.
export const SIDES = [{ name: "vk", launchCvars: { survival: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "resetlightlist", wad: buildResetLightListPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
