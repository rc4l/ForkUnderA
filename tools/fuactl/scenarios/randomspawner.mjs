// [rc4l] RandomSpawners under sv_nomonsters: one whose list names a class nobody defined, which crashed the spawner (zandronum@0f5e07570b01), and one with a monster it must pass over; exit 0 when each spawns the one item it may.
//
// usage:  node scenarios/randomspawner.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, addToCVar, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

export const COUNTER_CVAR = "zx_randomspawn_wrong";
const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const SPOT_TID = 10;

// [rc4l] The spawners puke 1 places, each at its own spot.
export const SPAWNERS = ["ZXSpawnUnknown", "ZXSpawnPastMonster"];

export const PLAYER = { x: 64, y: 128 };

export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 256, ROOM, () => ({ side: WALL }));
  SPAWNERS.forEach((_, i) => m.thing({ x: 400 + 200 * i, y: 128, angle: 0, type: THING.MapSpot, id: SPOT_TID + i }));
  m.thing({ ...PLAYER, angle: 0, type: 1 });
  return m;
}

// [rc4l] The unknown class weighs nothing, so the pick lands on the item every time and only the weighing walks over it.
export const DECORATE = `
Actor ZXSpawnGood
{
  Radius 8
  Height 16
  States
  {
  Spawn:
    BON1 A 0 NoDelay ACS_ExecuteAlways(3, 0)
    BON1 A -1
    Stop
  }
}

Actor ZXSpawnMonster
{
  Monster
  Radius 16
  Height 32
  States
  {
  Spawn:
    TROO A 0 NoDelay ACS_ExecuteAlways(2, 0)
    TROO A -1
    Stop
  }
}

Actor ZXSpawnUnknown : RandomSpawner
{
  DropItem "ZXNoSuchClass", 255, 0
  DropItem "ZXSpawnGood", 255, 1
}

Actor ZXSpawnPastMonster : RandomSpawner
{
  DropItem "ZXSpawnMonster", 255, 1
  DropItem "ZXSpawnGood", 255, 1
}
`;

// [rc4l] Script 1 counts each spawner in, and one more for itself so a puke that never ran reads 0, then spawns them; script 2 counts a monster that should not have come, script 3 counts an item out.
export function behavior() {
  const strings = [COUNTER_CVAR, ...SPAWNERS];
  const code = SPAWNERS.flatMap((_, i) => [...callFunc(ACSF.SpawnSpotForced, [1 + i, SPOT_TID + i, 0, 0]), PCD.DROP]);
  return acsScripts([
    { code: [...addToCVar(0, SPAWNERS.length + 1), ...code, PCD.TERMINATE], open: false, number: 1 },
    { code: [...addToCVar(0, 1), PCD.TERMINATE], open: false, number: 2 },
    { code: [...addToCVar(0, -1), PCD.TERMINATE], open: false, number: 3 },
  ], strings);
}

export function buildRandomSpawnerPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap(), { behavior: behavior() })) },
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: `map MAP01 "Random spawners"\n{\n}\n` },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const VANTAGES = [{ name: "west", at: { ...PLAYER, z: 0, angle: 0, pitch: 0 } }];

export const COUNTER = { command: COUNTER_CVAR, label: "spawners that spawned a monster or nothing, plus the puke's own one",
  pattern: `"${COUNTER_CVAR}" is "(-?\\d+)"`, ceiling: 1, floor: 1 };

export const SIDES = [{ name: "nomonsters", launchCvars: { ...MUTE, sv_nomonsters: 1 }, commands: ["puke 1"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "randomspawner", wad: buildRandomSpawnerPk3(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES, holdTics: 35 }), compareCounter);
