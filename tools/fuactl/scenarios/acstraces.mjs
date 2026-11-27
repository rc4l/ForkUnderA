// [Doruk-Aksoy] ACS line traces (features/acs-traces): exit 0 when fua_LineTrace stops where ZScript's LineTrace would on a wall, a BLOCKHITSCAN
// line, a step's upper and lower parts, a row of imps, a solid decoration, a 3D floor, deep water and the sky, every TR_ property reads back what it hit,
// fua_LineInfo and fua_SectorInfo read lines and sectors by index, fua_Trace's rules keep, pass and stop at what they match, callback functions
// (fixtures/acstraces/tracecb.bcs) decide the hits rules hand them, a script stops at 16 tracers with one warning, and tracers, their rules and
// callbacks survive a savegame.
//
// usage:  node scenarios/acstraces.mjs
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, AcsScript, PCD, SCRIPT } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { logged } from "./acshandles.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FN = { GetActorHandle: 24020, GetField: 24030, QueryCreate: 24040, TracerCreate: 24050, TracerFree: 24051, TracerClear: 24052,
  TraceRule: 24053, TraceCallback: 24054, LineTrace: 24055, Trace: 24056, TraceHitCount: 24057, TraceResult: 24058, TraceResultStr: 24059, LineInfo: 24060, SectorInfo: 24061 };
export const TR = { HitType: 0, HitActor: 1, HitX: 2, HitY: 3, HitZ: 4, DirX: 5, DirY: 6, DirZ: 7, Distance: 8, Fraction: 9, HitLine: 10, LineId: 11,
  LineSide: 12, LinePart: 13, HitSector: 14, SectorTag: 15, SectorPlane: 16, F3DFloor: 17, CrossedWater: 18, Crossed3DWater: 19, HitTexture: 20 };
export const TRF = { ABSPOSITION: 1, THRUSPECIES: 4, THRUACTORS: 8, THRUHITSCAN: 32, NOSKY: 64, ALLACTORS: 128, SOLIDACTORS: 256, FUA_PHYSICALSIZE: 1 << 20,
  FUA_ALLLINES: 1 << 21 };
export const LI = { FLAGS: 0, SPECIAL: 1, ARG0: 2, ID: 7, FRONTSECTOR: 8, BACKSECTOR: 9, X1: 12, Y1: 13, X2: 14, Y2: 15 };
export const SI = { FLOOR: 0, CEILING: 1, LIGHT: 2, TAG: 4 };
export const HIT = { NONE: 0, FLOOR: 1, CEILING: 2, WALL: 3, ACTOR: 4 };
// [Doruk-Aksoy] Rule kinds, actions and filters: TR_ kinds 64-69, TRACE_ actions, the FQ_ filters rules use and TQ_ 48-50.
export const KIND = { ANY: 64, FLOOR: 65, CEILING: 66, WALL: 67, ACTOR: 68, FFLOOR: 69 };
export const ACT = { STOP: 0, CONTINUE: 1, SKIP: 2, ABORT: 3, RECORD: 4 };
export const FQ = { CLASS: 1, SHOOTABLE: 8, TID: 10, SIGHT: 13, EXCLUDE: 14, FLAT: 15 };
export const TQ = { LINEFLAG: 48, SPECIAL: 49, TEXTURE: 50 };
const TAGS = ["w1", "w2", "b1", "b2", "p1", "p2", "a1", "a2", "c1", "f1", "f2", "h1", "s1", "s2", "i1", "l1", "x1", "v1", "v2", "v3", "n1",
  "r1", "r2", "r3", "r4", "r5"];
export const STRINGS = [...TAGS, " ", "|", "tid", "DoomImp", "BRICK1", "CEIL3_5", "NoSuchClass", "NOSUCHTEX"];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
// [Doruk-Aksoy] Tags: the room's sector and east wall, the BLOCKHITSCAN line, three 3D-floor and water targets; the imps' tids.
export const TAG = { ROOM: 3, EAST: 7, BLOCKHITSCAN: 8, FFLOOR: 10, POOL: 11, WATER3D: 12 };
export const TID = { IMP1: 31, IMP2: 32, IMP3: 33 };
const G = { T: 0, HELD: 1, RESUME: 2, A: 3, B: 4, FREED: 5 };
const GT = 22, SUBTRACT = 15, SPECIAL_ACS_EXECUTE = 80, ML_BLOCKHITSCAN = 0x08000000;
// [Doruk-Aksoy] ACS angles, a full turn being 1.0: almost straight up or down, and a dive of about 22 degrees.
const UP = -0.24 * 65536, DOWN = 0.24 * 65536, DIVE = 3971;

const fx = (n) => Math.round(n * 65536);
const fn = (name, ...args) => callFunc(FN[name], args);
const global = (n) => [PCD.PUSHGLOBALVAR, n];
const keep = (n, words) => [...words, PCD.ASSIGNGLOBALVAR, n];
const run = (...calls) => calls.flatMap((c) => [...c, PCD.DROP]);
const tr = (t, prop, ...hit) => fn("TraceResult", t, TR[prop], ...hit);
const tidOf = (h) => fn("GetField", h, S.tid);
const rule = (t, kind, action, ...filter) => fn("TraceRule", t, KIND[kind], ACT[action], ...filter);
// [Doruk-Aksoy] Clears the tracer, then gives it these rules, each [kind, action, filter...].
const rules = (t, ...list) => run(fn("TracerClear", t), ...list.map((r) => rule(t, ...r)));
// [Doruk-Aksoy] fua_Trace from a map point along a direction, east by default; `ignore` is a handle's words.
const trace = (t, { x, y, z, dx = 1, dy = 0, dz = 0, dist = 4096, flags = 0, ignore }) =>
  fn("Trace", t, fx(x), fx(y), fx(z), fx(dx), fx(dy), fx(dz), fx(dist), flags, ...(ignore === undefined ? [] : [ignore]));
const IMPS = { x: 100, y: 680, z: 32 }, STEP = { x: 100, y: 780, z: 32 }, NORTH = { x: 500, y: 900, z: 32, dx: 0, dy: 1 };
// [Doruk-Aksoy] fua_LineTrace from a map point by default; `h` and TRF_ABSPOSITION left out traces from that actor, x and y its forward and side offsets.
const lt = (t, { h = 0, x, y, z, angle = 0, dist = 4096, pitch = 0, flags = 0, abs = true }) =>
  fn("LineTrace", t, h, angle, fx(dist), pitch, flags | (abs ? TRF.ABSPOSITION : 0), fx(z), fx(x), fx(y));

// [Doruk-Aksoy] The map, built once so the judge knows each line's and sector's index.
export function buildMap() {
  const m = new UdmfMap();
  const ix = {};
  const lineCount = () => m.lines.length;
  // [Doruk-Aksoy] The north wall carries a special that is never set off, for fua_LineInfo; a 3D floor's own special is cleared once the floor is made.
  ix.room = m.room(0, 0, 2048, 1024, { ...ROOM, id: TAG.ROOM }, (edge) => ({ side: { texturemiddle: "STARTAN2" },
    line: edge === "east" ? { id: TAG.EAST } : edge === "north" ? { special: SPECIAL_ACS_EXECUTE, arg0: 42 } : {} }));
  ix.east = m.lines.findIndex((l) => l.id === TAG.EAST);
  ix.north = m.lines.findIndex((l) => l.special === SPECIAL_ACS_EXECUTE);
  // [Doruk-Aksoy] Lane y=880: a thin block whose west line blocks hitscans.
  ix.blockhitscan = lineCount();
  m.block(1000, 860, 1016, 900, ROOM, ix.room, () => ({}), () => ({}), (edge) => (edge === "west" ? { blockhitscan: true, id: TAG.BLOCKHITSCAN } : {}));
  // [Doruk-Aksoy] Lane y=780: a step whose floor and ceiling show its lower and upper parts.
  ix.stepWest = lineCount();
  ix.step = m.block(1500, 740, 1600, 820, { ...ROOM, heightfloor: 64, heightceiling: 160 }, ix.room, (edge) => (edge === "west" ? { texturebottom: "BRICK1", texturetop: "BRICK7" } : {}));
  // [Doruk-Aksoy] Lane y=440: a solid 3D floor from 16 to 64; lane y=320: Boom deep water to 48; lane y=200: a swimmable 3D floor to 48.
  ix.ffWest = lineCount();
  ix.ffTarget = m.block(1200, 400, 1400, 480, { ...ROOM, id: TAG.FFLOOR }, ix.room);
  ix.pool = m.block(1200, 280, 1400, 360, { ...ROOM, id: TAG.POOL }, ix.room);
  ix.water3dTarget = m.block(1200, 160, 1400, 240, { ...ROOM, texturefloor: "FLAT14", id: TAG.WATER3D }, ix.room);
  ix.sky = m.block(100, 40, 300, 140, { ...ROOM, textureceiling: "F_SKY1" }, ix.room);
  // [Doruk-Aksoy] The control sectors, sealed away east of the room.
  ix.ffControl = m.room(3000, 0, 3064, 64, { ...ROOM, heightfloor: 16, heightceiling: 64 }, (edge) => ({ side: { texturemiddle: "COMPBLUE" },
    line: edge === "west" ? { special: SPECIAL.Sector_Set3DFloor, arg0: TAG.FFLOOR, arg1: 1, arg2: 0, arg3: 255 } : {} }));
  m.room(3100, 0, 3164, 64, { ...ROOM, heightfloor: 48 }, (edge) => ({ side: { texturemiddle: "STARTAN2" },
    line: edge === "west" ? { special: SPECIAL.Transfer_Heights, arg0: TAG.POOL } : {} }));
  ix.water3dControl = m.room(3200, 0, 3264, 64, { ...ROOM, heightfloor: 0, heightceiling: 48, textureceiling: "FWATER1" }, (edge) => ({ side: { texturemiddle: "COMPBLUE" },
    line: edge === "west" ? { special: SPECIAL.Sector_Set3DFloor, arg0: TAG.WATER3D, arg1: 2, arg2: 0, arg3: 128 } : {} }));
  m.thing({ x: 40, y: 40, angle: 0, type: 1 });
  // [Doruk-Aksoy] Lane y=680: three imps facing away, deaf; lane y=560: a pillar, solid but not shootable.
  [TID.IMP1, TID.IMP2, TID.IMP3].forEach((id, i) => m.thing({ x: 600 + 100 * i, y: 680, angle: 0, type: THING.DoomImp, id, ambush: true }));
  m.thing({ x: 600, y: 560, angle: 0, type: THING.TechPillar });
  return { m, ix };
}
export const MAP = buildMap();
const ix = MAP.ix;

const t = global(G.T);
// [Doruk-Aksoy] Each script's checks: the words of a value and what it must be, a [want, slack] pair allowing that much either way.
export const CHECKS = {
  // [Doruk-Aksoy] The east wall from (100, 960, 32), every property.
  w1: [
    [lt(t, { x: 100, y: 960, z: 32 }), HIT.WALL],
    [tr(t, "HitActor"), 0], [tr(t, "HitX"), fx(2048)], [tr(t, "HitY"), fx(960)], [tr(t, "HitZ"), fx(32)],
    [tr(t, "DirX"), fx(1)], [tr(t, "DirY"), 0], [tr(t, "DirZ"), 0], [tr(t, "Distance"), fx(1948)], [tr(t, "Fraction"), [fx(1948 / 4096), 2]],
    [tr(t, "HitLine"), ix.east], [tr(t, "LineId"), TAG.EAST], [tr(t, "LineSide"), 0], [tr(t, "LinePart"), 1],
    [tr(t, "HitSector"), ix.room], [tr(t, "SectorTag"), TAG.ROOM], [tr(t, "SectorPlane"), 0], [tr(t, "F3DFloor"), -1],
    [tr(t, "CrossedWater"), -1], [tr(t, "Crossed3DWater"), -1], [tr(t, "HitTexture"), 0],
    [fn("TraceHitCount", t), 1], [tr(t, "HitX", 0), fx(2048)], [tr(t, "HitX", 1), 0], [fn("TraceResult", t, 99), 0],
    // [Doruk-Aksoy] 100 units falls short: nothing hit, no hit kept, the end point read back; a zero distance traces nothing.
    [lt(t, { x: 100, y: 960, z: 32, dist: 100 }), HIT.NONE], [tr(t, "HitX"), fx(200)], [tr(t, "Distance"), fx(100)], [fn("TraceHitCount", t), 0],
    [lt(t, { x: 100, y: 960, z: 32, dist: 0 }), 0],
    [[...run(fn("TracerClear", t)), ...tr(t, "HitLine")], -1],
  ],
  // [Doruk-Aksoy] The BLOCKHITSCAN line stops a trace unless TRF_THRUHITSCAN.
  b1: [
    [lt(t, { x: 100, y: 880, z: 32 }), HIT.WALL], [tr(t, "HitLine"), ix.blockhitscan], [tr(t, "LineId"), TAG.BLOCKHITSCAN], [tr(t, "LineSide"), 1],
    [tr(t, "HitX"), fx(1000)],
    [lt(t, { x: 100, y: 880, z: 32, flags: TRF.THRUHITSCAN }), HIT.WALL], [tr(t, "HitLine"), ix.east],
  ],
  // [Doruk-Aksoy] The step's lower part from z 32 and its upper part from z 200, both on its back side.
  p1: [
    [lt(t, { x: 100, y: 780, z: 32 }), HIT.WALL], [tr(t, "HitLine"), ix.stepWest], [tr(t, "LinePart"), 2], [tr(t, "LineSide"), 1], [tr(t, "HitSector"), ix.room],
    [lt(t, { x: 100, y: 780, z: 200 }), HIT.WALL], [tr(t, "LinePart"), 0],
  ],
  // [Doruk-Aksoy] The imps: the first from the player, the wall through them all, the second from the first, and the wall past its own kind.
  a1: [
    [lt(t, { x: 100, y: 680, z: 32 }), HIT.ACTOR], [tidOf(tr(t, "HitActor")), TID.IMP1], [tr(t, "HitX"), [fx(580), fx(1)]],
    [tr(t, "HitLine"), -1], [tr(t, "LinePart"), 0],
    [lt(t, { x: 100, y: 680, z: 32, flags: TRF.FUA_PHYSICALSIZE }), HIT.ACTOR], [tr(t, "HitX"), [fx(580), fx(1)]],
    [lt(t, { x: 100, y: 680, z: 32, flags: TRF.THRUACTORS }), HIT.WALL],
    [lt(t, { h: fn("GetActorHandle", TID.IMP1), x: 0, y: 0, z: 32, abs: false }), HIT.ACTOR], [tidOf(tr(t, "HitActor")), TID.IMP2],
    [lt(t, { h: fn("GetActorHandle", TID.IMP1), x: 0, y: 0, z: 32, abs: false, flags: TRF.THRUSPECIES }), HIT.WALL],
    [lt(t, { h: 999999, x: 0, y: 0, z: 32 }), 0],
  ],
  // [Doruk-Aksoy] The pillar is passed unless solid or all actors count.
  c1: [
    [lt(t, { x: 100, y: 560, z: 32 }), HIT.WALL],
    [lt(t, { x: 100, y: 560, z: 32, flags: TRF.SOLIDACTORS }), HIT.ACTOR],
    [lt(t, { x: 100, y: 560, z: 32, flags: TRF.ALLACTORS }), HIT.ACTOR],
  ],
  // [Doruk-Aksoy] The 3D floor's side names its control sector; above it the trace reaches the wall.
  f1: [
    [lt(t, { x: 100, y: 440, z: 32 }), HIT.WALL], [tr(t, "F3DFloor"), ix.ffControl], [tr(t, "HitLine"), ix.ffWest], [tr(t, "LinePart"), 1], [tr(t, "HitX"), fx(1200)],
    [lt(t, { x: 100, y: 440, z: 100 }), HIT.WALL], [tr(t, "F3DFloor"), -1], [tr(t, "HitLine"), ix.east],
  ],
  // [Doruk-Aksoy] Under Boom deep water, and diving into a swimmable 3D floor to the pool's floor.
  h1: [
    [lt(t, { x: 100, y: 320, z: 32 }), HIT.WALL], [tr(t, "CrossedWater"), ix.pool], [tr(t, "Crossed3DWater"), -1],
    [lt(t, { x: 1100, y: 200, z: 100, pitch: DIVE }), HIT.FLOOR], [tr(t, "Crossed3DWater"), ix.water3dControl], [tr(t, "HitSector"), ix.water3dTarget],
    [tr(t, "CrossedWater"), -1], [tr(t, "SectorPlane"), 0],
  ],
  // [Doruk-Aksoy] Ceilings and floors: the sky unless TRF_NOSKY, the room's ceiling and floor.
  s1: [
    [lt(t, { x: 200, y: 90, z: 32, pitch: UP }), HIT.CEILING], [tr(t, "HitSector"), ix.sky], [tr(t, "SectorPlane"), 1],
    [lt(t, { x: 200, y: 90, z: 32, pitch: UP, flags: TRF.NOSKY }), HIT.NONE],
    [lt(t, { x: 200, y: 960, z: 32, pitch: UP }), HIT.CEILING], [tr(t, "HitZ"), [fx(256), fx(1)]], [tr(t, "SectorPlane"), 1], [tr(t, "HitLine"), -1],
    [lt(t, { x: 200, y: 960, z: 32, pitch: DOWN }), HIT.FLOOR], [tr(t, "HitZ"), [0, fx(1)]], [tr(t, "SectorPlane"), 0], [tr(t, "DirZ"), [-fx(0.998), fx(0.01)]],
  ],
  // [Doruk-Aksoy] Lines and sectors by index.
  i1: [
    [fn("LineInfo", ix.east, LI.ID), TAG.EAST], [fn("LineInfo", ix.east, LI.FRONTSECTOR), ix.room], [fn("LineInfo", ix.east, LI.BACKSECTOR), -1],
    [fn("LineInfo", ix.east, LI.X1), fx(2048)], [fn("LineInfo", ix.east, LI.Y1), fx(1024)], [fn("LineInfo", ix.east, LI.X2), fx(2048)], [fn("LineInfo", ix.east, LI.Y2), 0],
    [fn("LineInfo", ix.blockhitscan, LI.FLAGS), { bits: ML_BLOCKHITSCAN }], [fn("LineInfo", ix.blockhitscan, LI.BACKSECTOR), ix.room],
    [fn("LineInfo", ix.north, LI.SPECIAL), SPECIAL_ACS_EXECUTE], [fn("LineInfo", ix.north, LI.ARG0), 42],
    [fn("LineInfo", -1, LI.ID), 0], [fn("LineInfo", 999999, LI.ID), 0], [fn("LineInfo", ix.east, 99), 0],
    [fn("SectorInfo", ix.room, SI.FLOOR), 0], [fn("SectorInfo", ix.room, SI.CEILING), fx(256)], [fn("SectorInfo", ix.room, SI.LIGHT), 192],
    [fn("SectorInfo", ix.room, SI.TAG), TAG.ROOM], [fn("SectorInfo", ix.step, SI.FLOOR), fx(64)], [fn("SectorInfo", -1, SI.FLOOR), 0], [fn("SectorInfo", ix.room, 99), 0],
  ],
  // [Doruk-Aksoy] Sixteen tracers a script, two refused; a tracer's id follows a query's, as they share one serial.
  l1: [
    [[...keep(G.A, fn("QueryCreate")), ...fn("TracerCreate"), ...global(G.A), SUBTRACT], 1],
    [[PCD.PUSHNUMBER, 0, ...Array.from({ length: 15 }, () => [...fn("TracerCreate"), PCD.PUSHNUMBER, 0, GT, PCD.ADD]).flat()], 15],
    [fn("TracerCreate"), 0], [fn("TracerCreate"), 0],
  ],
  // [Doruk-Aksoy] Another script's tracer and a freed one find nothing.
  x1: [
    [fn("TraceHitCount", global(G.HELD)), 0], [tr(global(G.HELD), "HitType"), 0], [lt(global(G.HELD), { x: 100, y: 960, z: 32 }), 0],
    [[...keep(G.B, fn("TracerCreate")), ...run(fn("TracerFree", global(G.B))), ...lt(global(G.B), { x: 100, y: 960, z: 32 })], 0],
  ],
  // [Doruk-Aksoy] After the load: the saved tracers' hits, then a new tracer past the freed id.
  n1: [[[...fn("TracerCreate"), ...global(G.FREED), GT], 1]],
  // [Doruk-Aksoy] Rules on the imps: recording through them, stopping at a tid, none, aborting, going on, a handle and a class filter, an ignored actor.
  r1: [
    [[...rules(t, ["ACTOR", "RECORD", FQ.SHOOTABLE], ["ACTOR", "SKIP"], ["WALL", "STOP"]), ...trace(t, IMPS)], 4],
    [tidOf(tr(t, "HitActor", 0)), TID.IMP1], [tidOf(tr(t, "HitActor", 1)), TID.IMP2], [tidOf(tr(t, "HitActor", 2)), TID.IMP3],
    [tr(t, "HitType", 3), HIT.WALL], [tr(t, "HitLine"), ix.east], [tr(t, "HitSector", 0), ix.room],
    [[...rules(t, ["ACTOR", "STOP", FQ.TID, TID.IMP2], ["ACTOR", "SKIP"], ["WALL", "STOP"]), ...trace(t, IMPS)], 1], [tidOf(tr(t, "HitActor", 0)), TID.IMP2],
    [[...rules(t), ...trace(t, IMPS)], 1], [tidOf(tr(t, "HitActor", 0)), TID.IMP1],
    [[...rules(t, ["ACTOR", "ABORT"]), ...trace(t, IMPS)], 0], [tr(t, "HitType"), HIT.NONE], [fn("TraceHitCount", t), 0],
    [[...rules(t, ["ACTOR", "CONTINUE"], ["WALL", "STOP"]), ...trace(t, IMPS)], 1], [tr(t, "HitType", 0), HIT.WALL],
    [[...rules(t, ["ACTOR", "STOP", FQ.EXCLUDE, fn("GetActorHandle", TID.IMP1)], ["ACTOR", "SKIP"]), ...trace(t, IMPS)], 1], [tidOf(tr(t, "HitActor", 0)), TID.IMP2],
    [[...rules(t, ["ACTOR", "STOP", FQ.CLASS, S.DoomImp]), ...trace(t, IMPS)], 1], [tidOf(tr(t, "HitActor", 0)), TID.IMP1],
    [[...rules(t), ...trace(t, { x: 600, y: 680, z: 32, ignore: fn("GetActorHandle", TID.IMP1) })], 1], [tidOf(tr(t, "HitActor", 0)), TID.IMP2],
    [trace(t, { ...IMPS, ignore: 999999 }), 0], [trace(t, { ...IMPS, dx: 0 }), 0], [trace(t, { ...IMPS, dist: 0 }), 0], [trace(999999, IMPS), 0],
    // [Doruk-Aksoy] fua_LineTrace keeps to its flags and leaves the rules for the next fua_Trace.
    [[...rules(t, ["ACTOR", "SKIP"]), ...lt(t, IMPS)], HIT.ACTOR], [trace(t, IMPS), 1], [tr(t, "HitType", 0), HIT.WALL],
  ],
  // [Doruk-Aksoy] Rules on lines: a line flag, every line crossed, a special kept or turned round, a texture, and a 3D floor's side apart from walls.
  r2: [
    [[...rules(t, ["WALL", "RECORD", TQ.LINEFLAG, ML_BLOCKHITSCAN], ["WALL", "STOP"]), ...trace(t, { x: 100, y: 880, z: 32 })], 2],
    [tr(t, "HitLine", 0), ix.blockhitscan], [tr(t, "HitLine", 1), ix.east],
    [[...rules(t, ["WALL", "RECORD"]), ...trace(t, { x: 100, y: 880, z: 32 })], 2],
    [trace(t, { x: 100, y: 880, z: 32, flags: TRF.FUA_ALLLINES }), 3], [tr(t, "HitLine", 1), ix.blockhitscan + 2],
    [[...rules(t, ["WALL", "RECORD", TQ.SPECIAL, SPECIAL_ACS_EXECUTE], ["WALL", "STOP"]), ...trace(t, NORTH)], 1], [tr(t, "HitLine", 0), ix.north], [tr(t, "HitType"), HIT.NONE],
    [[...rules(t, ["WALL", "STOP", TQ.SPECIAL, SPECIAL_ACS_EXECUTE, 0], ["ANY", "ABORT"]), ...trace(t, NORTH)], 0],
    [[...rules(t, ["WALL", "RECORD", TQ.TEXTURE, S.BRICK1], ["ANY", "STOP"]), ...trace(t, STEP)], 2], [tr(t, "HitLine", 0), ix.stepWest],
    [[...rules(t, ["WALL", "ABORT"], ["FFLOOR", "STOP"]), ...trace(t, { x: 100, y: 440, z: 32 })], 1], [tr(t, "F3DFloor", 0), ix.ffControl],
  ],
  // [Doruk-Aksoy] Rules on planes: a floor kept or passed (it ends the trace either way), the sky with and without TRF_NOSKY, a ceiling by texture.
  r3: [
    [[...rules(t, ["FLOOR", "RECORD"]), ...trace(t, { x: 200, y: 960, z: 32, dz: -1 })], 1], [tr(t, "HitType", 0), HIT.FLOOR], [tr(t, "HitX", 0), [fx(232), fx(1)]],
    [[...rules(t, ["FLOOR", "SKIP"]), ...trace(t, { x: 200, y: 960, z: 32, dz: -1 })], 0], [tr(t, "HitType"), HIT.FLOOR],
    [[...rules(t, ["CEILING", "RECORD"]), ...trace(t, { x: 200, y: 90, z: 32, dx: 0.05, dz: 1 })], 1], [tr(t, "HitSector", 0), ix.sky],
    [trace(t, { x: 200, y: 90, z: 32, dx: 0.05, dz: 1, flags: TRF.NOSKY }), 0], [tr(t, "HitType"), HIT.NONE],
    [[...rules(t, ["CEILING", "STOP", TQ.TEXTURE, S.CEIL3_5], ["ANY", "ABORT"]), ...trace(t, { x: 200, y: 960, z: 32, dx: 0.05, dz: 1 })], 1],
  ],
  // [Doruk-Aksoy] Rules refused: no tracer, a hit type for a kind, a bad action, a shape filter, filters on kinds they can't test, unknown names
  // and handles (added all the same), a filter without its argument, then 32 rules and a 33rd.
  r4: [
    [[...run(fn("TracerClear", t)), ...fn("TraceRule", 999999, KIND.ACTOR, ACT.STOP)], 0], [fn("TraceRule", t, HIT.ACTOR, ACT.STOP), 0],
    [fn("TraceRule", t, KIND.WALL, 9), 0], [rule(t, "ACTOR", "STOP", FQ.FLAT), 0], [rule(t, "WALL", "STOP", FQ.TID, 3), 0],
    [rule(t, "ACTOR", "STOP", TQ.SPECIAL, 80), 0], [rule(t, "ACTOR", "STOP", FQ.CLASS, S.NoSuchClass), 0], [rule(t, "WALL", "STOP", TQ.TEXTURE, S.NOSUCHTEX), 0],
    [rule(t, "ACTOR", "STOP", FQ.SIGHT, 999999), 0], [rule(t, "ACTOR", "STOP", FQ.TID), 0],
    [fn("TraceCallback", t, 12345), 0], [fn("TraceCallback", 999999, 0), 0], [fn("TraceCallback", t, 0), 1],
    [[PCD.PUSHNUMBER, 0, ...Array.from({ length: 29 }, () => [...rule(t, "WALL", "STOP"), PCD.ADD]).flat()], 29], [rule(t, "WALL", "STOP"), 0],
  ],
};
// [Doruk-Aksoy] String logs: each value's words and the texture name it must give.
export const STRING_CHECKS = {
  w2: [[fn("TraceResultStr", t, TR.HitTexture), ""]],
  p2: [[[...run(lt(t, { x: 100, y: 780, z: 32 })), ...fn("TraceResultStr", t, TR.HitTexture)], "BRICK1"],
    [[...run(lt(t, { x: 100, y: 780, z: 200 })), ...fn("TraceResultStr", t, TR.HitTexture)], "BRICK7"],
    [[...run(lt(t, { x: 100, y: 960, z: 32 })), ...fn("TraceResultStr", t, TR.HitTexture)], "STARTAN2"],
    [fn("TraceResultStr", t, TR.HitX), ""], [fn("TraceResultStr", t, TR.HitTexture, 5), ""]],
  b2: [[[...run(lt(t, { x: 100, y: 880, z: 32 })), ...fn("TraceResultStr", t, TR.HitTexture)], ""]],
  a2: [[[...run(lt(t, { x: 100, y: 680, z: 32 })), ...fn("TraceResultStr", t, TR.HitTexture)], ""]],
  f2: [[[...run(lt(t, { x: 100, y: 440, z: 32 })), ...fn("TraceResultStr", t, TR.HitTexture)], "COMPBLUE"]],
  s2: [[[...run(lt(t, { x: 200, y: 960, z: 32, pitch: UP })), ...fn("TraceResultStr", t, TR.HitTexture)], "CEIL3_5"],
    [[...run(lt(t, { x: 200, y: 960, z: 32, pitch: DOWN })), ...fn("TraceResultStr", t, TR.HitTexture)], "FLOOR4_8"]],
  r5: [[[...rules(t, ["WALL", "RECORD", TQ.TEXTURE, S.BRICK1], ["ANY", "STOP"]), ...run(trace(t, STEP)), ...fn("TraceResultStr", t, TR.HitTexture, 0)], "BRICK1"]],
};

function logWords(tag, values) {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG];
}
function logStrings(tag, values) {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S["|"], PCD.PRINTSTRING, ...v, PCD.PRINTSTRING]), PCD.PUSHNUMBER, S["|"], PCD.PRINTSTRING, PCD.ENDLOG];
}
const checksOf = (tag) => logWords(tag, CHECKS[tag].map(([w]) => w));
const stringsOf = (tag) => logStrings(tag, STRING_CHECKS[tag].map(([w]) => w));

// [Doruk-Aksoy] A tracer is freed with its script, so each script makes its own first.
const traced = (...parts) => [...keep(G.T, fn("TracerCreate")), ...parts.flat(), PCD.TERMINATE];
export const SCRIPTS = {
  1: traced(checksOf("w1"), stringsOf("w2")),
  2: traced(checksOf("b1"), stringsOf("b2")),
  3: traced(checksOf("p1"), stringsOf("p2")),
  4: traced(checksOf("a1"), stringsOf("a2")),
  5: traced(checksOf("c1")),
  6: traced(checksOf("f1"), stringsOf("f2")),
  7: traced(checksOf("h1")),
  8: traced(checksOf("s1"), stringsOf("s2")),
  9: [...checksOf("i1"), PCD.TERMINATE],
  10: [...checksOf("l1"), PCD.TERMINATE],
  // [Doruk-Aksoy] A tracer held while another script tries its id.
  11: [...keep(G.HELD, fn("TracerCreate")), ...run(lt(global(G.HELD), { x: 100, y: 960, z: 32 })), PCD.DELAYDIRECT, 105, PCD.TERMINATE],
  12: [...checksOf("x1"), PCD.TERMINATE],
  13: [PCD.PUSHNUMBER, 1, PCD.ASSIGNGLOBALVAR, G.RESUME, PCD.TERMINATE],
  14: [...checksOf("n1"), PCD.TERMINATE],
  15: traced(checksOf("r1")),
  16: traced(checksOf("r2"), stringsOf("r5")),
  17: traced(checksOf("r3")),
  18: traced(checksOf("r4")),
};

// [Doruk-Aksoy] Script 20 holds three tracers, one on an imp, one on the wall and one with rules, and waits across a savegame; it goes first in the
// lump so its offsets start at 0.
export function waitingScript() {
  const s = new AcsScript();
  const a = [PCD.PUSHSCRIPTVAR, 0], b = [PCD.PUSHSCRIPTVAR, 1], r = [PCD.PUSHSCRIPTVAR, 2];
  s.emit(...fn("TracerCreate"), PCD.ASSIGNSCRIPTVAR, 0, ...fn("TracerCreate"), PCD.ASSIGNSCRIPTVAR, 1, ...fn("TracerCreate"), PCD.ASSIGNSCRIPTVAR, 2,
    ...keep(G.FREED, fn("TracerCreate")), ...run(fn("TracerFree", global(G.FREED))),
    ...run(rule(r, "ACTOR", "SKIP", FQ.TID, TID.IMP1), rule(r, "ACTOR", "STOP", FQ.CLASS, S.DoomImp), rule(r, "WALL", "STOP")),
    ...logWords("v1", [lt(a, { x: 100, y: 680, z: 32 }), lt(b, { x: 100, y: 960, z: 32 })]));
  s.label("wait").emit(...global(G.RESUME)).jump(PCD.IFGOTO, "done").emit(PCD.DELAYDIRECT, 1).jump(PCD.GOTO, "wait");
  s.label("done").emit(...logWords("v2", [fn("TraceHitCount", a), tidOf(tr(a, "HitActor")), tr(b, "HitLine"), tr(b, "HitX", 0),
    trace(r, IMPS), tidOf(tr(r, "HitActor", 0))]),
    ...logStrings("v3", [fn("TraceResultStr", b, TR.HitTexture)]), PCD.TERMINATE);
  return s.words(0);
}

// [Doruk-Aksoy] The callback functions, compiled by zt-bcc from tracecb.bcs beside it, as a library.
export const CALLBACKS = fileURLToPath(new URL("../fixtures/acstraces/tracecb.o", import.meta.url));

// [Doruk-Aksoy] `withScripts` changes the script list and `withMap` the map (things only, so the judge's indexes hold), as netacstraces' copies and starts do.
export function buildAcsTracesPk3({ withScripts = (s) => s, withMap = () => {} } = {}) {
  const scripts = withScripts([{ code: waitingScript(), type: SCRIPT.CLOSED, number: 20 },
    ...Object.entries(SCRIPTS).map(([number, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(number) }))]);
  const { m } = buildMap();
  withMap(m);
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS) })) },
    { name: "acs/tracecb.o", data: fs.readFileSync(CALLBACKS) }, { name: "LOADACS.txt", data: "tracecb\n" }]);
}

// [Doruk-Aksoy] The strings a log printed between bars, or null.
export function loggedStrings(text, tag) {
  const m = new RegExp(`${tag}\\|(.*)\\|`).exec(String(text));
  return m ? m[1].split("|") : null;
}

export const FULL_WARNING = /fua_TracerCreate: a script already holds 16 tracers/g;
export const CALLBACK_WARNING = /fua_TraceCallback: a callback waited/g;
const want = (checks) => checks.map(([, w]) => w);
export const WANT = {
  ...Object.fromEntries(Object.keys(CHECKS).map((tag) => [tag, want(CHECKS[tag])])),
  v1: [HIT.ACTOR, HIT.WALL],
  v2: [1, TID.IMP1, ix.east, fx(2048), 1, TID.IMP2],
  // [Doruk-Aksoy] tracecb.bcs: pierced two imps, the second's tid, two seen; skipped to the wall; a callback or 99 returned stops at the first imp;
  // a callback that waits stops there too; a nested trace refused keeps all four hits; cleared, and with no callback the first imp stops it.
  cb1: [2, TID.IMP1, TID.IMP2, 2, 1, HIT.WALL, 1, TID.IMP1, 1, 1, TID.IMP1, 4, 1, 1],
  // [Doruk-Aksoy] A callback kept across the savegame records all three imps; one that removes the second imp ahead stops at the third.
  cb2: [4, TID.IMP3],
  cb3: [1, TID.IMP3],
};
export const WANT_STRINGS = { ...Object.fromEntries(Object.keys(STRING_CHECKS).map((tag) => [tag, want(STRING_CHECKS[tag])])), v3: ["STARTAN2"] };

// [Doruk-Aksoy] Whether a logged value is what a check wants: exact, within [want, slack], or with every bit of { bits } set.
export function fits(got, w) {
  if (Array.isArray(w)) return Math.abs(got - w[0]) <= w[1];
  if (w !== null && typeof w === "object") return (got & w.bits) === w.bits;
  return got === w;
}

// [Doruk-Aksoy] What is wrong with each log, by index, against `want` and `strings` (WANT and WANT_STRINGS, or a scenario's own tags of them).
export function judgeTraces(logs, want = WANT, strings = WANT_STRINGS) {
  const faults = [];
  for (const [tag, values] of Object.entries(want)) {
    const got = logged(logs[tag], tag);
    if (!got) { faults.push(`script ${tag} logged nothing (${String(logs[tag]).trim().slice(0, 100)})`); continue; }
    values.forEach((w, i) => { if (!fits(got[i], w)) faults.push(`${tag}[${i}] is ${got[i]}, not ${JSON.stringify(w)}`); });
  }
  for (const [tag, values] of Object.entries(strings)) {
    const got = loggedStrings(logs[tag], tag);
    if (!got) { faults.push(`script ${tag} logged no strings`); continue; }
    values.forEach((w, i) => { if (got[i] !== w) faults.push(`${tag}[${i}] is "${got[i]}", not "${w}"`); });
  }
  const warnings = (String(logs.l1).match(FULL_WARNING) || []).length;
  if ("l1" in want && warnings !== 1) faults.push(`a full script warned ${warnings} times, not once`);
  const waits = (String(logs.cb1).match(CALLBACK_WARNING) || []).length;
  if ("cb1" in want && waits !== 1) faults.push(`a waiting callback warned ${waits} times, not once`);
  return faults;
}

// [Doruk-Aksoy] The scripts run one after another before the savegame, each with the tags its log goes under.
export const TRACE_STEPS = [[1, ["w1", "w2"]], [2, ["b1", "b2"]], [3, ["p1", "p2"]], [4, ["a1", "a2"]], [5, ["c1"]], [6, ["f1", "f2"]], [7, ["h1"]],
  [8, ["s1", "s2"]], [9, ["i1"]], [10, ["l1"]], [11, []], [12, ["x1"]], [15, ["r1"]], [16, ["r2", "r5"]], [17, ["r3"]], [18, ["r4"]]];

// [Doruk-Aksoy] A locked launch comes up without monsters unless asked.
export const SIDES = [{ name: "vk", launchCvars: { ...MUTE, sv_nomonsters: 0 } }];

export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  const enter = async (command) => {
    await exec(command);
    await cap.waitInLevel(c, undefined, { pressThrough: true });
    await cap.waitTics(c, 5);
  };
  await cap.waitTics(c, 10);
  const logs = {};
  for (const [n, tags] of TRACE_STEPS) {
    const out = await exec(`puke ${n}`);
    for (const tag of tags) logs[tag] = out;
  }
  logs.cb1 = await exec("puke 30");
  await exec("puke 31");
  logs.v1 = await exec("puke 20");
  await exec("save fuactl_acstraces.zds");
  await cap.waitTics(c, 10);
  // [Doruk-Aksoy] A new game zeroes the id counter, so only the savegame brings it back.
  await enter("map MAP01");
  await enter("load fuactl_acstraces.zds");
  const resumed = await exec("puke 13");
  logs.v2 = resumed;
  logs.v3 = resumed;
  logs.cb2 = resumed;
  logs.n1 = await exec("puke 14");
  logs.cb3 = await exec("puke 32");
  const faults = judgeTraces(logs);
  const count = Object.values(WANT).reduce((n, v) => n + v.length, 0) + Object.values(WANT_STRINGS).reduce((n, v) => n + v.length, 0);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${count} trace values right on walls, lines, steps, actors, 3D floors, water and sky, rules and callbacks keeping, passing and stopping at what they match, 16 tracers with one warning, and tracers, rules and callbacks kept across a savegame`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acstraces", wad: buildAcsTracesPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
