// [rc4l] A late joiner's light list under a 3D floor whose sector's ceiling the server lowered (zandronum@38e71b91375c): exit 0 when the joiner's list agrees with the server's, where applying the lowered ceiling kept the list's copy of the old one.
//
// usage:  node scenarios/netlightlist.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acsScripts, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import { clientAgreement } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const ROOM_TAG = 5, LOWER_BY = 64;
// [rc4l] The script the server runs: the 3D-floored room's ceiling down LOWER_BY at once.
export const LOWER_SCRIPT = [...callSpecial(SPECIAL.Ceiling_LowerByValue, [ROOM_TAG, 255, LOWER_BY]), PCD.TERMINATE];

export function buildNetLightListPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { ...ROOM, id: ROOM_TAG }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 32, angle: 90, type });
  // [rc4l] The 3D floor, 64 to 96 high and darker than the room, so the room's light list has an entry of its own.
  m.room(640, 0, 704, 64, { heightfloor: 64, heightceiling: 96, texturefloor: "FLAT14", textureceiling: "FLAT14", lightlevel: 96 }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: ROOM_TAG, arg1: 1, arg2: 0, arg3: 255 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: LOWER_SCRIPT, open: false, number: 1 }], []) })) }]);
}

// [rc4l] What is wrong with the server's room before the joiner is compared with it: its ceiling lowered, and its light list renewed to start there.
export function judgeServerRoom(row) {
  const want = ROOM.heightceiling - LOWER_BY;
  if (!row) return ["the server reported no room sector"];
  const faults = [];
  if (Math.abs(row.ceiling - want) > 0.5) faults.push(`the server's room ceiling is ${row.ceiling}, not ${want}`);
  if (!(row.lights?.length > 1)) faults.push(`the server's room has light list ${row.lights}, not one under a 3D floor`);
  else if (Math.abs(row.lights[0] - want) > 0.5) faults.push(`the server's light list starts at ${row.lights[0]}, not its ceiling's ${want}`);
  return faults;
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  await cap.exec(sc, "puke 1");
  await cap.waitTics(sc, 70);
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 70);
  const room = (await sc.rpc("world.sectorstate", { first: 0, count: 1 }))?.sectors?.[0];
  const faults = judgeServerRoom(room);
  const agreed = await clientAgreement(sc, set.clients, label);
  if (!faults.length) return agreed;
  return { lines: [`FAIL ${label}: ${faults.join("; ")}`, ...agreed.lines], failed: agreed.failed + 1 };
}

export const CONFIGS = [{ name: "latejoin", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netlightlist", wad: buildNetLightListPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
