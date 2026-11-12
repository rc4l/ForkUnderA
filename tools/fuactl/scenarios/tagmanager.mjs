// [rc4l] Sector tags and line IDs as zandronum@a17262422dfc through @762c1585fac5's FTagManager keeps them, each map checked by a puke that finds sectors by tag and lines by ID; exit 0 when every map's counter reads 1.
//
// usage:  node scenarios/tagmanager.mjs
import { UdmfMap, wad, mapLumps, hexenMapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, addToCVar, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

export const COUNTER_CVAR = "zx_tagmanager_wrong";
const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };
const WALL = { texturemiddle: "STARTAN2" };
const PLAYER = { x: 960, y: 448 };
// [rc4l] Activation bits as lines[].activation holds them (SPAC_*), one per line under test so a wrong line reads as a wrong value.
export const SPAC = { Cross: 1, Use: 2, Impact: 8, Push: 16 };
const LINE_SET_IDENTIFICATION = 121;

// [rc4l] The column standing in MAP01's untagged sector, which ThingCountSector and ThingCountNameSector find with tag 0.
export const COLUMN = { x: 160, y: 160, tid: 11, type: THING.TallGreenColumn, name: "TallGreenColumn" };
const STRINGS = [COUNTER_CVAR, COLUMN.name];

// [rc4l] The words that leave each kind of check's value on the stack: a sector's light by tag, a line's activation by ID, or a count of the column by tid or class in sectors of a tag.
const READ = {
  sector: (tag) => [PCD.PUSHNUMBER, tag, PCD.GETSECTORLIGHTLEVEL],
  line: (id) => callFunc(ACSF.GetLineActivation, [id]),
  count: (tag) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, COLUMN.tid, PCD.PUSHNUMBER, tag, PCD.THINGCOUNTSECTOR],
  countname: (tag) => [PCD.PUSHNUMBER, STRINGS.indexOf(COLUMN.name), PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, tag, PCD.THINGCOUNTNAMESECTOR],
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
};

// [rc4l] 1 for the puke itself, then 2^(k+1) for each failing check k, so the counter says which checks failed.
export function buildScript(checks) {
  const s = new AcsScript().emit(...addToCVar(0, 1));
  checks.forEach(([, kind, id, want], k) => {
    s.emit(...READ[kind](id), PCD.PUSHNUMBER, want, PCD.EQ).jump(PCD.IFGOTO, `ok${k}`).emit(...addToCVar(0, 2 ** (k + 1))).label(`ok${k}`);
  });
  return s.emit(PCD.TERMINATE).words();
}

// [rc4l] The failing checks' names from a counter's value.
export function failedChecks(map, value) {
  return CHECKS[map].filter((_, k) => (value >> (k + 1)) & 1).map(([what]) => what);
}

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
  room(m, { id: 3, lightlevel: 160 }, { north: { special: LINE_SET_IDENTIFICATION, arg0: 7, arg4: 1, playeruse: true } });
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

const MAPS = { MAP01: buildMoreIdsMap, MAP02: buildHexenMap, MAP03: () => buildLinelessMap(false), MAP04: () => buildLinelessMap(true) };

export function buildTagManagerPk3() {
  const behavior = (map) => acsScripts([{ code: buildScript(CHECKS[map]), open: false, number: 1 }], STRINGS);
  const lumps = (map) => (map === "MAP02" ? hexenMapLumps(map, MAPS[map](), { behavior: behavior(map) })
    : mapLumps(map, MAPS[map](), { behavior: behavior(map) }));
  return pk3([
    ...Object.keys(MAPS).map((map) => ({ name: `maps/${map}.wad`, data: wad(lumps(map)) })),
    { name: "mapinfo.txt", data: Object.keys(MAPS).map((map) => `map ${map} "Tags ${map}"\n{\n}\n`).join("") },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const COUNTER = { command: COUNTER_CVAR, label: "1 for the puke, plus 2^(k+1) for each failing check k of the map's CHECKS",
  pattern: `"${COUNTER_CVAR}" is "(-?\\d+)"`, ceiling: 1, floor: 1,
  explain: (map, value) => (value < 1 ? ["the puke never ran"] : failedChecks(map, value)) };

runScenarioIfMain(import.meta.url, () => ({ name: "tagmanager", wad: buildTagManagerPk3(), maps: Object.keys(MAPS),
  vantages: [{ name: "east", at: { ...PLAYER, z: 0, angle: 180, pitch: 0 } }], counter: COUNTER,
  sides: [{ name: "tags", launchCvars: { ...MUTE }, commands: ["puke 1"] }] }), compareCounter);
