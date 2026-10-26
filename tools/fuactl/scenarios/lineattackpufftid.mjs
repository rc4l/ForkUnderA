// [Doruk-Aksoy] A map spot firing ACS LineAttack at a wall with a pufftid, the ninth argument: exit 0 when the puff it leaves carries that tid, as Zandronum 3.3 and UZDoom give it, rather than the argument being ignored.
//
// usage:  node scenarios/lineattackpufftid.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acs0, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

const SHOOTER = 5, PUFF = 77;
const RANGE = 2048 * 65536;

// [Doruk-Aksoy] LineAttack(shooter, east, level, no damage, "BulletPuff", no damage type, 2048, no flags, pufftid), then the things carrying the puff's tid are counted into the cvar.
export const BEHAVIOR = acs0([PCD.DELAYDIRECT, 5,
  ...callFunc(ACSF.LineAttack, [SHOOTER, 0, 0, 0, 1, 0, RANGE, 0, PUFF]), PCD.DROP,
  PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, PUFF, PCD.THINGCOUNT, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP,
  PCD.TERMINATE], ["zx_pufftid_found", "BulletPuff"]);

export function buildLineAttackPuffTidPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 128, y: 256, angle: 0, type: THING.MapSpot, id: SHOOTER });
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: BEHAVIOR })) },
    { name: "cvarinfo.txt", data: "server int zx_pufftid_found = 0;\n" },
  ]);
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

export const COUNTER = { command: "zx_pufftid_found", label: "puffs carrying the pufftid",
  pattern: "\"zx_pufftid_found\" is \"(\\d+)\"", floor: 1 };

export const SIDES = [{ name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "lineattackpufftid", wad: buildLineAttackPuffTidPk3(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES }), compareCounter);
