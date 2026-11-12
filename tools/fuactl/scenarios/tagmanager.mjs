// [rc4l] Sector tags and line IDs as zandronum@a17262422dfc through @762c1585fac5's FTagManager keeps them, each map checked by a puke that finds sectors by tag and lines by ID; exit 0 when every map's counter reads 1.
//
// usage:  node scenarios/tagmanager.mjs
import { UdmfMap, wad, mapLumps, hexenMapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, checksScript, checksCounter, ACSF, PCD } from "../src/acs.mjs";
import { THING, SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

export const COUNTER_CVAR = "zx_tagmanager_wrong";
const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };
const WALL = { texturemiddle: "STARTAN2" };
const PLAYER = { x: 960, y: 448 };
// [rc4l] Activation bits as lines[].activation holds them (SPAC_*), one per line under test so a wrong line reads as a wrong value.
export const SPAC = { Cross: 1, Use: 2, Impact: 8, Push: 16 };
const FIXED = 65536;

// [rc4l] The column standing in MAP01's untagged sector, which ThingCountSector and ThingCountNameSector find with tag 0.
export const COLUMN = { x: 160, y: 160, tid: 11, type: THING.TallGreenColumn, name: "TallGreenColumn" };
const STRINGS = [COUNTER_CVAR, COLUMN.name];
// [rc4l] The carrying maps' barrels by tid, the first of each pair in the west block and the second in the east one.
export const BARRELS = { 12: { x: 160, y: 160 }, 13: { x: 608, y: 160 }, 14: { x: 160, y: 160 }, 15: { x: 608, y: 160 } };
// [rc4l] How far east a carried barrel must be after the wait, MAP05's scroller tag, and MAP06's conveyor tag and line ID.
const CARRIED = 16, SCROLL_TAG = 9, CONVEYOR = { tag: 7, id: 5 };
// [rc4l] Boom's carrying conveyor, Scroll_Floor along its line once xlat translates it.
const BOOM_CONVEYOR = 252;
export const CARRY_TICS = 35;

// [rc4l] The words that leave each kind of check's value on the stack: a sector's light by tag, a line's activation by ID, or a count of the column by tid or class in sectors of a tag.
export const READ = {
  sector: (tag) => [PCD.PUSHNUMBER, tag, PCD.GETSECTORLIGHTLEVEL],
  line: (id) => callFunc(ACSF.GetLineActivation, [id]),
  count: (tag) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, COLUMN.tid, PCD.PUSHNUMBER, tag, PCD.THINGCOUNTSECTOR],
  countname: (tag) => [PCD.PUSHNUMBER, STRINGS.indexOf(COLUMN.name), PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, tag, PCD.THINGCOUNTNAMESECTOR],
  carried: (tid) => [PCD.PUSHNUMBER, tid, PCD.GETACTORX, PCD.PUSHNUMBER, (BARRELS[tid].x + CARRIED) * FIXED, PCD.GE],
  still: (tid) => [PCD.PUSHNUMBER, tid, PCD.GETACTORX, PCD.PUSHNUMBER, (BARRELS[tid].x + CARRIED) * FIXED, PCD.LT],
};

// [rc4l] Each map's checks as [what, kind, tag or ID, expected], read as READ[kind] does.
export const CHECKS = {
  // [rc4l] UDMF ids and moreids on sectors and lines, sector 0 and line 0 among them.
  MAP01: [
    ["sector 0's tag (zandronum@90fabec29540)", "sector", 3, 160],
    ["tag 0 finds the first untagged sector (zandronum@75690467d5f1)", "sector", 0, 144],
    ["a sector's id is its own, not the tag list's index (zandronum@566770b828b4)", "sector", 4, 128],
    ["a sector's moreids (zandronum@a17262422dfc)", "sector", 5, 128],
    ["a sector's last moreid", "sector", 6, 128],
    ["line 0's id (zandronum@90fabec29540)", "line", 7, SPAC.Impact],
    ["a line's id", "line", 8, SPAC.Push],
    ["a line's moreids (zandronum@020ebf903625)", "line", 10, SPAC.Push],
    ["ThingCountSector's tag 0 counts untagged sectors (zandronum@0bf8926534b3)", "count", 0, 1],
    ["ThingCountNameSector's tag 0 counts untagged sectors (zandronum@0bf8926534b3)", "countname", 0, 1],
    ["ThingCountSector by tag skips the column's untagged sector", "count", 3, 0],
  ],
  // [rc4l] A Hexen-format map, whose loader numbered its lines backwards until zandronum@566770b828b4.
  MAP02: [
    ["Line_SetIdentification's id, high byte in arg4, on line 1", "line", 7 + 256, SPAC.Use],
    ["a binary sector's tag", "sector", 3, 160],
  ],
  // [rc4l] No tags at all and a sector with no lines, which zandronum@e517153e644c stopped crashing.
  MAP03: [],
  // [rc4l] A sector with no lines past the last tagged one, outside the tag list until zandronum@58b963341e90 checked its size.
  MAP04: [["the tagged sector before it", "sector", 3, 160]],
  // [rc4l] A floor scroller that carries, and an untagged sector copying it, which zandronum@6fe64e17fb1e stopped refusing.
  MAP05: [
    ["the scroller carries its tagged sector's barrel", "carried", 12, 1],
    ["Sector_CopyScroller gives an untagged sector the scroller (zandronum@6fe64e17fb1e)", "carried", 13, 1],
  ],
  // [rc4l] A doom-namespace map whose Boom conveyor names its sector in arg0 and has a line ID naming another, which it took for its tag until zandronum@a4ccbf23d1b7.
  MAP06: [
    ["a translated special's tag is its arg0 (zandronum@a4ccbf23d1b7)", "carried", 14, 1],
    ["the sector the line's ID names stays put", "still", 15, 1],
  ],
};
// [rc4l] The maps whose checks wait for the level to act first.
const DELAY = { MAP05: CARRY_TICS, MAP06: CARRY_TICS };

function room(m, props, walls) {
  return m.room(0, 0, 1024, 512, { ...ROOM, ...props }, (edge) => ({ side: WALL, line: walls[edge] || {} }));
}

function playerStart(m) {
  m.thing({ ...PLAYER, angle: 180, type: 1 });
}

// [rc4l] Sector 0 the room, sector 1 untagged, sector 2 an id and moreids; line 0 the west wall and the north wall's id and moreids.
export function buildMoreIdsMap() {
  const m = new UdmfMap();
  const outer = room(m, { id: 3, lightlevel: 160 }, { west: { id: 7, impact: true }, north: { id: 8, moreids: "9 10", playerpush: true } });
  m.block(64, 64, 256, 256, { ...ROOM, lightlevel: 144 }, outer);
  m.block(512, 64, 704, 256, { ...ROOM, id: 4, moreids: "5 6", lightlevel: 128 }, outer);
  m.thing({ x: COLUMN.x, y: COLUMN.y, angle: 0, type: COLUMN.type, id: COLUMN.tid });
  playerStart(m);
  return m;
}

// [rc4l] Line 1, the north wall, sets its ID and is the only one used by pressing.
export function buildHexenMap() {
  const m = new UdmfMap();
  room(m, { id: 3, lightlevel: 160 }, { north: { special: SPECIAL.Line_SetIdentification, arg0: 7, arg4: 1, playeruse: true } });
  playerStart(m);
  return m;
}

// [rc4l] A room and a sector nothing borders, with tagged the room's tag or none.
export function buildLinelessMap(tagged) {
  const m = new UdmfMap();
  room(m, tagged ? { id: 3, lightlevel: 160 } : { lightlevel: 160 }, {});
  m.sector({ ...ROOM, lightlevel: 96 });
  playerStart(m);
  return m;
}

// [rc4l] The carrying maps' room: line, the walls' lines by edge, a west and an east block styled west and east, eastLine(edge) the east block's lines, and a barrel in each block.
function carryRoom(m, { line, west, east, eastLine = () => ({}), tids }) {
  const outer = room(m, { lightlevel: 160 }, line);
  m.block(64, 64, 256, 256, { ...ROOM, ...west, lightlevel: 160 }, outer);
  m.block(512, 64, 704, 256, { ...ROOM, ...east, lightlevel: 160 }, outer, () => ({}), () => ({}), eastLine);
  for (const tid of tids) m.thing({ ...BARRELS[tid], angle: 0, type: THING.ExplosiveBarrel, id: tid });
  playerStart(m);
  return m;
}

// [rc4l] The south wall's Scroll_Floor carries east in the tagged west block, and the untagged east block's west line copies it for carrying (arg1 bit 4).
export function buildCopyScrollerMap() {
  return carryRoom(new UdmfMap(), { line: { south: { special: SPECIAL.Scroll_Floor, arg0: SCROLL_TAG, arg2: 1, arg3: 192, arg4: 128 } },
    west: { id: SCROLL_TAG }, east: {}, eastLine: (edge) => (edge === "west" ? { special: SPECIAL.Sector_CopyScroller, arg0: SCROLL_TAG, arg1: 4 } : {}),
    tids: [12, 13] });
}

// [rc4l] The north wall, drawn west to east, is the conveyor: arg0 the west block's tag, its line ID the east block's.
export function buildConveyorMap() {
  return carryRoom(new UdmfMap({ namespace: "doom" }), { line: { north: { special: BOOM_CONVEYOR, arg0: CONVEYOR.tag, id: CONVEYOR.id } },
    west: { id: CONVEYOR.tag }, east: { id: CONVEYOR.id }, tids: [14, 15] });
}

const MAPS = { MAP01: buildMoreIdsMap, MAP02: buildHexenMap, MAP03: () => buildLinelessMap(false), MAP04: () => buildLinelessMap(true),
  MAP05: buildCopyScrollerMap, MAP06: buildConveyorMap };

export function buildTagManagerPk3() {
  const behavior = (map) => acsScripts([{ code: checksScript(CHECKS[map], READ, { delayTics: DELAY[map] || 0 }), open: false, number: 1 }], STRINGS);
  const lumps = (map) => (map === "MAP02" ? hexenMapLumps(map, MAPS[map](), { behavior: behavior(map) })
    : mapLumps(map, MAPS[map](), { behavior: behavior(map) }));
  return pk3([
    ...Object.keys(MAPS).map((map) => ({ name: `maps/${map}.wad`, data: wad(lumps(map)) })),
    { name: "mapinfo.txt", data: Object.keys(MAPS).map((map) => `map ${map} "Tags ${map}"\n{\n}\n`).join("") },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const COUNTER = checksCounter(COUNTER_CVAR, (map) => CHECKS[map]);

runScenarioIfMain(import.meta.url, () => ({ name: "tagmanager", wad: buildTagManagerPk3(), maps: Object.keys(MAPS),
  vantages: [{ name: "east", at: { ...PLAYER, z: 0, angle: 180, pitch: 0 } }], counter: COUNTER,
  sides: [{ name: "tags", launchCvars: { ...MUTE }, commands: ["puke 1"] }], holdTics: 2 * CARRY_TICS }), compareCounter);
