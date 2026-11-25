// [Doruk-Aksoy] Every kind of water an actor's waterlevel reads (a waterzone, swimmable and non-swimmable Boom deep water at three depths, swimmable 3D floors), with barrels standing in each and dropped into three, checked by a puke; exit 0 when the counter reads 1.
//
// usage:  node scenarios/waterlevel.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, checksScript, checksCounter, PCD } from "../src/acs.mjs";
import { THING, SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

export const COUNTER_CVAR = "zx_waterlevel_wrong";
// [Doruk-Aksoy] GetActorProperty's number for APROP_WaterLevel (p_acs.cpp).
export const APROP_WATERLEVEL = 28;
const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const STEP = { texturebottom: "STARTAN2" };
const PLAYER = { x: 2304, y: 384 };
// [Doruk-Aksoy] Tags of the swimmable and the non-swimmable Boom pools and of the deep and shallow 3D floors.
export const TAG = { swim: 10, still: 11, deep3d: 12, shallow3d: 13 };
// [Doruk-Aksoy] Transfer_Heights' flag that makes the water swimmable, and Sector_Set3DFloor's swimmable type.
const SWIMMABLE = 8, FF_SWIMMABLE = 2;

// [Doruk-Aksoy] The pools as [name, x0, y0, floor, tag or waterzone]: Boom pools take their surface (0) from their control sector.
export const POOLS = [
  ["waterzone", 128, 128, 0, "waterzone"],
  ["boom deep", 512, 128, -128, TAG.swim],
  ["boom waist", 896, 128, -30, TAG.swim],
  ["boom feet", 1280, 128, -10, TAG.swim],
  ["boom non-swimmable", 512, 448, -128, TAG.still],
  ["3d deep", 896, 448, 0, TAG.deep3d],
  ["3d shallow", 1280, 448, 0, TAG.shallow3d],
];
const POOL = 256;

// [Doruk-Aksoy] Each barrel as [what, tid, pool name or null for the dry floor, spawn height, waterlevel], a barrel 42 high: under the surface by over its height is 3, its middle is 2, its foot is 1.
export const BARRELS = [
  ["a barrel on the dry floor reads 0", 1, null, 0, 0],
  ["a barrel in a waterzone sector reads 3", 2, "waterzone", 0, 3],
  ["a barrel under swimmable Boom water reads 3", 3, "boom deep", 0, 3],
  ["a barrel waist-deep in swimmable Boom water reads 2", 4, "boom waist", 0, 2],
  ["a barrel ankle-deep in swimmable Boom water reads 1", 5, "boom feet", 0, 1],
  ["a barrel under non-swimmable Boom water keeps 0 (the reset)", 6, "boom non-swimmable", 0, 0],
  ["a barrel inside a swimmable 3D floor reads 3", 7, "3d deep", 0, 3],
  ["a barrel ankle-deep in a swimmable 3D floor reads 1", 8, "3d shallow", 0, 1],
  ["a barrel dropped into swimmable Boom water reads 3", 9, "boom deep", 192, 3],
  ["a barrel dropped into non-swimmable Boom water keeps 0", 10, "boom non-swimmable", 192, 0],
  ["a barrel dropped through a swimmable 3D floor's top reads 3", 11, "3d deep", 160, 3],
];
// [Doruk-Aksoy] Tics the puke waits, long enough for the dropped barrels to settle in water.
export const SETTLE_TICS = 105;

// [Doruk-Aksoy] The words that leave a barrel's waterlevel on the stack.
export const READ = { water: (tid) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, APROP_WATERLEVEL, PCD.GETACTORPROPERTY] };
export const CHECKS = BARRELS.map(([what, tid, , , want]) => [what, "water", tid, want]);

// [Doruk-Aksoy] Where a barrel stands: spread across its pool by its order among the pool's barrels, or east on the dry floor.
function spot(pool, tid) {
  if (pool === null) return { x: 1792, y: 256 };
  const [, x0, y0] = POOLS.find(([name]) => name === pool);
  const order = BARRELS.filter(([, , p]) => p === pool).findIndex(([, t]) => t === tid);
  return { x: x0 + 64 + 64 * order, y: y0 + 128 };
}

// [Doruk-Aksoy] The room of pools, a control room per tag north of it, and the barrels.
export function buildWaterMap() {
  const m = new UdmfMap();
  const outer = m.room(0, 0, 2560, 768, ROOM, () => ({ side: WALL }));
  for (const [, x0, y0, floor, kind] of POOLS) {
    const props = kind === "waterzone" ? { waterzone: true } : { id: kind };
    m.block(x0, y0, x0 + POOL, y0 + POOL, { ...ROOM, heightfloor: floor, texturefloor: "FWATER1", ...props }, outer, () => STEP, () => STEP);
  }
  // [Doruk-Aksoy] Boom control sectors put the surface at 0; a 3D floor's control sector spans its water from its floor to its ceiling.
  const controls = [
    [TAG.swim, { heightfloor: 0, texturefloor: "FWATER1" }, { special: SPECIAL.Transfer_Heights, arg0: TAG.swim, arg1: SWIMMABLE }],
    [TAG.still, { heightfloor: 0, texturefloor: "FWATER1" }, { special: SPECIAL.Transfer_Heights, arg0: TAG.still, arg1: 0 }],
    [TAG.deep3d, { heightfloor: 0, heightceiling: 64, texturefloor: "FWATER1", textureceiling: "FWATER1" }, { special: SPECIAL.Sector_Set3DFloor, arg0: TAG.deep3d, arg1: FF_SWIMMABLE }],
    [TAG.shallow3d, { heightfloor: 0, heightceiling: 16, texturefloor: "FWATER1", textureceiling: "FWATER1" }, { special: SPECIAL.Sector_Set3DFloor, arg0: TAG.shallow3d, arg1: FF_SWIMMABLE }],
  ];
  controls.forEach(([, props, line], i) => {
    const x0 = 256 * i;
    m.room(x0, 1024, x0 + 128, 1152, { ...ROOM, ...props }, (edge) => ({ side: WALL, line: edge === "south" ? line : {} }));
  });
  for (const [, tid, pool, height] of BARRELS) m.thing({ ...spot(pool, tid), ...(height ? { height } : {}), angle: 0, type: THING.ExplosiveBarrel, id: tid });
  m.thing({ ...PLAYER, angle: 180, type: 1 });
  return m;
}

export function buildWaterLevelPk3() {
  const behavior = acsScripts([{ code: checksScript(CHECKS, READ, { delayTics: SETTLE_TICS }), open: false, number: 1 }], [COUNTER_CVAR]);
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildWaterMap(), { behavior })) },
    { name: "mapinfo.txt", data: `map MAP01 "Water levels"\n{\n}\n` },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
    // [Doruk-Aksoy] Doom defines no water terrain, so the pools' and the 3D water's flat gets the Water splash, which a drop into each spawns.
    { name: "terrain.txt", data: "floor FWATER1 Water\n" },
  ]);
}

export const COUNTER = checksCounter(COUNTER_CVAR, () => CHECKS);

runScenarioIfMain(import.meta.url, () => ({ name: "waterlevel", wad: buildWaterLevelPk3(), maps: ["MAP01"],
  vantages: [{ name: "west", at: { ...PLAYER, z: 0, angle: 180, pitch: 0 } }], counter: COUNTER,
  sides: [{ name: "water", launchCvars: { ...MUTE }, commands: ["puke 1"] }], holdTics: SETTLE_TICS + 35 }), compareCounter);
