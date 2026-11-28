// [Doruk-Aksoy] UZDoom's ACS functions and actor properties at UZDoom's numbers (features/acs-parity): exit 0 when script 1 logs UZDoom's answer to every check, DamageActor on a missing tid gives -1,
// [Doruk-Aksoy] and SetSectorDamage hurts the player once a 32-tic beat and stops when cleared (script 3, as UZDoom does), with an unknown type doing normal damage and one warning (script 4, ours).
//
// usage:  node scenarios/acsparity.mjs              ours, against the answers below
//         node scenarios/acsparity.mjs --uzdoom     UZDoom (FUACTL_UZDOOM), against its own: scripts 1 and 3, as script 2 crashes it
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, ACSF, PCD, SCRIPT, callFunc, AcsScript } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { runUzdoom } from "../src/uzdoom.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [Doruk-Aksoy] A flat this pk3's TERRAIN names, since Doom's own gives no flat a terrain, and ZxIce, which only SetSectorTerrain puts on a plane.
const MUD_FLAT = "CEIL3_5";
export const TERRAIN = `terrain ZxMud\n{\n}\nterrain ZxIce\n{\n}\nfloor ${MUD_FLAT} ZxMud\n`;
export const SOUTH_LINE = 7, DIAGONAL_LINE = 8, ROOM_TAG = 5, POOL_TAG = 6, MUD_TAG = 9;
export const DIAGONAL = [[100, 300], [160, 380], [220, 300]];
export const BARREL = { near: 11, mud: 12, far: 13, wet: 14, net: 15 };
// [Doruk-Aksoy] Where each barrel stands, so a judge finds it in world.actors, and the pool's water, 48 deep.
export const BARREL_AT = { near: [256, 128], mud: [160, 330], far: [420, 64], wet: [420, 420], net: [480, 410] };
export const POOL = { x0: 400, y0: 400, x1: 500, y1: 500, top: 48 };
// [Doruk-Aksoy] Ice, TRANSLATION(TRANSLATION_Standard, 7), which SetTranslation leaves the far and mud barrels on.
export const ICE = (3 << 16) | 7;
export const APROP = { Health: 0, TargetTID: 26, MaxStepHeight: 44, MaxDropOffHeight: 45, DamageType: 46, SoundClass: 47, FriendlySeeBlocks: 48, WaterDepth: 49 };
const CPXF = { LESSOREQUAL: 0x2, SETTARGET: 0x40, FARTHEST: 0x200 };
// [Doruk-Aksoy] GetLevelInfo's total monsters and GetPlayerInfo's fviewbob, at zcommon.bcs' numbers.
const LEVELINFO_TOTAL_MONSTERS = 7, PLAYERINFO_FVIEWBOB = 10;

export const STRINGS = ["parity", " ", "<", ">", "zxstrarg", "DoomImp", "ZxNoClass", "Thinker", "Clip", "Shell", "Health", "Spawn", "Pain.Fire",
  "ZxNope", "ExplosiveBarrel", "Fire", "fire", "damagenull ", "sectordamage", "ZxPlasma", "sdh", "Ice", "ZxNoTranslation", "", "SHOOTABLE", "ZxNoFlag", "COUNTKILL", "zxmarine", "sc", "ZxIce",
  "ZxNoTerrain", "terr"];
export const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));

const fixed = (v) => Math.round(v * 65536);
const call = (fn, ...args) => callFunc(ACSF[fn], args);
const getProp = (tid, prop) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, prop, PCD.GETACTORPROPERTY];
const setProp = (tid, prop, value) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, prop, PCD.PUSHNUMBER, value, PCD.SETACTORPROPERTY];
const positive = (words) => [PCD.PUSHNUMBER, 0, ...words, PCD.LT];

// [Doruk-Aksoy] UZDoom's GetLineX/Y and DoubleToACS: v1 + delta * along plus the unit normal (dy, -dx) times offset, rounded half to even.
export function lineCoord(axis, [x1, y1], [x2, y2], along, offset) {
  const dx = x2 - x1, dy = y2 - y1, len = Math.hypot(dx, dy);
  const a = along / 65536, o = offset / 65536;
  const units = (axis ? y1 + dy * a : x1 + dx * a) + (len ? ((axis ? -dx : dy) / len) * o : 0);
  const scaled = units * 65536, floor = Math.floor(scaled), frac = scaled - floor;
  return frac === 0.5 ? (floor % 2 === 0 ? floor : floor + 1) : Math.round(scaled);
}

// [Doruk-Aksoy] Each check pushes one value, in order, with UZDoom's answer; `str` checks push an ACS string.
export const CHECKS = [
  { what: "Floor(1.5)", words: call("Floor", fixed(1.5)), want: fixed(1) },
  { what: "Floor(-1.5)", words: call("Floor", fixed(-1.5)), want: fixed(-2) },
  { what: "Round(2.5)", words: call("Round", fixed(2.5)), want: fixed(3) },
  { what: "Round(-1.5)", words: call("Round", fixed(-1.5)), want: fixed(-1) },
  { what: "Ceil(1.25)", words: call("Ceil", fixed(1.25)), want: fixed(2) },
  // [Doruk-Aksoy] UZDoom answers 3.0 here, its (x & ~0xffff) + 0x10000 adding one to a whole number.
  { what: "Ceil(2.0)", words: call("Ceil", fixed(2)), want: fixed(2), uzdoom: fixed(3) },
  { what: "Ceil(-1.5)", words: call("Ceil", fixed(-1.5)), want: fixed(-1) },
  { what: "CheckClass(DoomImp)", words: call("CheckClass", S.DoomImp), want: 1 },
  { what: "CheckClass(ZxNoClass)", words: call("CheckClass", S.ZxNoClass), want: 0 },
  { what: "CheckClass(Thinker)", words: call("CheckClass", S.Thinker), want: 0 },
  { what: "GetMaxInventory(Clip)", words: call("GetMaxInventory", 0, S.Clip), want: 200 },
  { what: "GetMaxInventory(Shell), not carried", words: call("GetMaxInventory", 0, S.Shell), want: 50 },
  { what: "GetMaxInventory(Health)", words: call("GetMaxInventory", 0, S.Health), want: 100 },
  { what: "GetMaxInventory(ZxNoClass)", words: call("GetMaxInventory", 0, S.ZxNoClass), want: 0 },
  { what: "CheckActorState(Spawn)", words: call("CheckActorState", 0, S.Spawn), want: 1 },
  { what: "CheckActorState(Pain.Fire)", words: call("CheckActorState", 0, S["Pain.Fire"]), want: 1 },
  { what: "CheckActorState(Pain.Fire, exact)", words: call("CheckActorState", 0, S["Pain.Fire"], 1), want: 0 },
  { what: "CheckActorState(ZxNope)", words: call("CheckActorState", 0, S.ZxNope), want: 0 },
  // [Doruk-Aksoy] Our default terrain is spelled Solid, UZDoom's SOLID.
  { what: "GetActorFloorTerrain(player)", words: call("GetActorFloorTerrain", 0), want: "Solid", uzdoom: "SOLID", str: true },
  { what: "GetActorFloorTerrain(barrel on mud)", words: call("GetActorFloorTerrain", BARREL.mud), want: "ZxMud", str: true },
  { what: "GetActorFloorTerrain(no actor)", words: call("GetActorFloorTerrain", 999), want: "", str: true },
  { what: "GetActorFloorTerrain(barrel in the pool), UDMF floorterrain", words: call("GetActorFloorTerrain", BARREL.wet), want: "ZxIce", str: true },
  // [Doruk-Aksoy] The mud floor takes a script's terrain, keeps it through a plane naming neither floor nor ceiling and through a ceiling's change, and an unknown name gives the flat's back.
  // [Doruk-Aksoy] UZDoom's barrel keeps the terrain it last moved onto (AActor::floorterrain), so it reads ZxMud until it moves.
  { what: "SetSectorTerrain(mud, floor, ZxIce)", words: [...call("SetSectorTerrain", MUD_TAG, 0, S.ZxIce), PCD.DROP, ...call("GetActorFloorTerrain", BARREL.mud)], want: "ZxIce", uzdoom: "ZxMud", str: true },
  { what: "SetSectorTerrain(mud, plane 2)", words: [...call("SetSectorTerrain", MUD_TAG, 2, S.ZxNoTerrain), PCD.DROP, ...call("GetActorFloorTerrain", BARREL.mud)], want: "ZxIce", uzdoom: "ZxMud", str: true },
  { what: "SetSectorTerrain(mud, ceiling)", words: [...call("SetSectorTerrain", MUD_TAG, 1, S.ZxNoTerrain), PCD.DROP, ...call("GetActorFloorTerrain", BARREL.mud)], want: "ZxIce", uzdoom: "ZxMud", str: true },
  { what: "SetSectorTerrain(mud, floor, unknown)", words: [...call("SetSectorTerrain", MUD_TAG, 0, S.ZxNoTerrain), PCD.DROP, ...call("GetActorFloorTerrain", BARREL.mud)], want: "ZxMud", str: true },
  { what: "GetLineX(south wall, 0.25)", words: call("GetLineX", SOUTH_LINE, fixed(0.25), 0), want: lineCoord(0, [512, 0], [0, 0], fixed(0.25), 0) },
  { what: "GetLineY(south wall, 0.5, 16)", words: call("GetLineY", SOUTH_LINE, fixed(0.5), fixed(16)), want: lineCoord(1, [512, 0], [0, 0], fixed(0.5), fixed(16)) },
  { what: "GetLineX(diagonal, 0.3, 10)", words: call("GetLineX", DIAGONAL_LINE, fixed(0.3), fixed(10)), want: lineCoord(0, DIAGONAL[0], DIAGONAL[1], fixed(0.3), fixed(10)) },
  { what: "GetLineY(diagonal, 0.3, 10)", words: call("GetLineY", DIAGONAL_LINE, fixed(0.3), fixed(10)), want: lineCoord(1, DIAGONAL[0], DIAGONAL[1], fixed(0.3), fixed(10)) },
  { what: "GetLineX(no line)", words: call("GetLineX", 999, 0, 0), want: 0 },
  { what: "CheckProximity(barrel, 100)", words: call("CheckProximity", 0, S.ExplosiveBarrel, fixed(100)), want: 1 },
  { what: "CheckProximity(barrel, 200, 2)", words: call("CheckProximity", 0, S.ExplosiveBarrel, fixed(200), 2), want: 1 },
  { what: "CheckProximity(barrel, 200, 3)", words: call("CheckProximity", 0, S.ExplosiveBarrel, fixed(200), 3), want: 0 },
  { what: "CheckProximity(barrel, 200, 3, LESSOREQUAL)", words: call("CheckProximity", 0, S.ExplosiveBarrel, fixed(200), 3, CPXF.LESSOREQUAL), want: 1 },
  { what: "CheckProximity(barrel, 300, 1, SETTARGET|FARTHEST)", words: call("CheckProximity", 0, S.ExplosiveBarrel, fixed(300), 1, CPXF.SETTARGET | CPXF.FARTHEST), want: 1 },
  { what: "the target CheckProximity set", words: getProp(0, APROP.TargetTID), want: BARREL.mud },
  { what: "APROP_MaxStepHeight", words: getProp(0, APROP.MaxStepHeight), want: fixed(24) },
  { what: "APROP_MaxDropOffHeight", words: getProp(0, APROP.MaxDropOffHeight), want: fixed(24) },
  { what: "APROP_MaxStepHeight set to 40", words: [...setProp(0, APROP.MaxStepHeight, fixed(40)), ...getProp(0, APROP.MaxStepHeight)], want: fixed(40) },
  { what: "CheckActorProperty(MaxStepHeight, 40)", words: call("CheckActorProperty", 0, APROP.MaxStepHeight, fixed(40)), want: 1 },
  { what: "APROP_DamageType set to Fire", words: [...setProp(0, APROP.DamageType, S.Fire), ...getProp(0, APROP.DamageType)], want: "Fire", str: true },
  { what: "CheckActorProperty(DamageType, fire)", words: call("CheckActorProperty", 0, APROP.DamageType, S.fire), want: 1 },
  { what: "DamageActor(barrel, 10, Fire)", words: call("DamageActor", BARREL.near, 0, 0, 0, 10, S.Fire), want: 10 },
  { what: "the barrel's health after", words: getProp(BARREL.near, APROP.Health), want: 10 },
  { what: "StrArg ran the named script", words: [PCD.PUSHMAPVAR, 0], want: 1 },
  { what: "StrArg is negative", words: [...call("StrArg", S.zxstrarg), PCD.PUSHNUMBER, 0, PCD.LT], want: 1 },
  { what: "GetNetID(barrel) is above 0", words: positive(call("GetNetID", BARREL.near, 0)), want: 1 },
  { what: "GetNetID(barrel, second)", words: call("GetNetID", BARREL.near, 1), want: 0 },
  { what: "GetNetID(no actor)", words: call("GetNetID", 999, 0), want: 0 },
  // [Doruk-Aksoy] The player's sound class, while the player is still the activator.
  { what: "APROP_SoundClass, the player's class's", words: getProp(0, APROP.SoundClass), want: "player", str: true },
  { what: "APROP_SoundClass set to zxmarine", words: [...setProp(0, APROP.SoundClass, S.zxmarine), ...getProp(0, APROP.SoundClass)], want: "zxmarine", str: true },
  { what: "CheckActorProperty(SoundClass, zxmarine)", words: call("CheckActorProperty", 0, APROP.SoundClass, S.zxmarine), want: 1 },
  { what: "APROP_SoundClass set to \"\"", words: [...setProp(0, APROP.SoundClass, S[""]), ...getProp(0, APROP.SoundClass)], want: "player", str: true },
  { what: "SetActivatorByNetID(0)", words: call("SetActivatorByNetID", 0), want: 0 },
  { what: "SetActivatorByNetID(barrel)", words: call("SetActivatorByNetID", call("GetNetID", BARREL.near, 0)), want: 1 },
  { what: "the new activator's health", words: getProp(0, APROP.Health), want: 10 },
  { what: "APROP_FriendlySeeBlocks, its class's", words: getProp(BARREL.far, APROP.FriendlySeeBlocks), want: 10 },
  { what: "APROP_FriendlySeeBlocks set to 25", words: [...setProp(BARREL.far, APROP.FriendlySeeBlocks, 25), ...getProp(BARREL.far, APROP.FriendlySeeBlocks)], want: 25 },
  { what: "APROP_FriendlySeeBlocks set to -4", words: [...setProp(BARREL.far, APROP.FriendlySeeBlocks, -4), ...getProp(BARREL.far, APROP.FriendlySeeBlocks)], want: -4 },
  { what: "CheckActorProperty(FriendlySeeBlocks, -4)", words: call("CheckActorProperty", BARREL.far, APROP.FriendlySeeBlocks, -4), want: 1 },
  { what: "APROP_WaterDepth, dry", words: getProp(BARREL.mud, APROP.WaterDepth), want: 0 },
  { what: "APROP_WaterDepth, in the pool", words: getProp(BARREL.wet, APROP.WaterDepth), want: fixed(POOL.top) },
  { what: "CheckActorProperty(WaterDepth, 48)", words: call("CheckActorProperty", BARREL.wet, APROP.WaterDepth, fixed(POOL.top)), want: 1 },
  { what: "SetTranslation(far, Ice)", words: call("SetTranslation", BARREL.far, S.Ice), want: 1 },
  { what: "SetTranslation(mud, Ice), then an unknown name", words: [...call("SetTranslation", BARREL.mud, S.Ice), PCD.DROP, ...call("SetTranslation", BARREL.mud, S.ZxNoTranslation)], want: 1 },
  { what: "SetTranslation(wet, Ice), then \"\"", words: [...call("SetTranslation", BARREL.wet, S.Ice), PCD.DROP, ...call("SetTranslation", BARREL.wet, S[""])], want: 1 },
  { what: "SetActorFlag(far, SHOOTABLE, off)", words: call("SetActorFlag", BARREL.far, S.SHOOTABLE, 0), want: 1 },
  { what: "CheckFlag(far, SHOOTABLE) after", words: call("CheckFlag", BARREL.far, S.SHOOTABLE), want: 0 },
  { what: "SetActorFlag(far, ZxNoFlag)", words: call("SetActorFlag", BARREL.far, S.ZxNoFlag, 1), want: 0 },
  { what: "SetActorFlag(far, COUNTKILL), then the map's total monsters", words: [...call("SetActorFlag", BARREL.far, S.COUNTKILL, 1), PCD.DROP, PCD.PUSHNUMBER, LEVELINFO_TOTAL_MONSTERS, PCD.GETLEVELINFO], want: 1 },
  { what: "GetPlayerInfo(0, PLAYERINFO_FVIEWBOB)", words: [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, PLAYERINFO_FVIEWBOB, PCD.GETPLAYERINFO], want: 1 },
];

// [Doruk-Aksoy] What is wrong with a machine's barrels after script 1, from its world.actors rows: far and mud on Ice, wet given back its class's (near is pushed by DamageActor, so isn't used).
export function judgeTranslations(rows, want = { far: ICE, mud: ICE, wet: 0 }) {
  const at = ([x, y]) => (rows || []).find((r) => Math.abs(r.x - x) < 1 && Math.abs(r.y - y) < 1);
  return Object.entries(want).flatMap(([name, want]) => {
    const r = at(BARREL_AT[name]);
    if (!r) return [`no ${name} barrel in world.actors`];
    return r.tr === want ? [] : [`the ${name} barrel's translation is ${r.tr}, not ${want}`];
  });
}

// [Doruk-Aksoy] The words that print one check's value after a space, a string inside < >.
const printValue = (c) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...(c.str
  ? [PCD.PUSHNUMBER, S["<"], PCD.PRINTSTRING, ...c.words, PCD.PRINTSTRING, PCD.PUSHNUMBER, S[">"], PCD.PRINTSTRING]
  : [...c.words, PCD.PRINTNUMBER])];

// [Doruk-Aksoy] Script 1 runs the named script through ACS_ExecuteAlways(StrArg(name)), waits for it, then logs "parity" and each check's value.
export function buildScript() {
  return [PCD.PUSHNUMBER, S.zxstrarg, PCD.CALLFUNC, 1, ACSF.StrArg, PCD.PUSHNUMBER, 0, PCD.LSPEC1 + 1, SPECIAL.ACS_ExecuteAlways, PCD.DELAYDIRECT, 2,
    PCD.BEGINPRINT, PCD.PUSHNUMBER, S.parity, PCD.PRINTSTRING, ...CHECKS.flatMap(printValue), PCD.ENDLOG, PCD.TERMINATE];
}

// [Doruk-Aksoy] Script 2 logs what DamageActor gives for a tid no actor holds, which crashed P_DamageMobj before its null check.
export function buildNullDamageScript() {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S["damagenull "], PCD.PRINTSTRING, ...call("DamageActor", 999, 0, 0, 0, 10, S.Fire), PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE];
}

// [Doruk-Aksoy] SetSectorDamage is ACS function 94 at UZDoom's number; the PCD numbers of Timer, Modulus and PlayerHealth.
export const SET_SECTOR_DAMAGE = 94;
const TIMER = 93, MODULUS = 18;
export const PLAYERHEALTH = 120;

// [Doruk-Aksoy] Scripts 3 and 4: each step waits for tic 16 of the 32-tic beat sectors damage on, so a 32-tic wait holds exactly one hurt tic, then logs the player's health.
export function sectorDamageScript(steps) {
  const s = new AcsScript();
  // [Doruk-Aksoy] Each health goes in a script variable and all print at the end, since a Delay ends the print buffer.
  steps.forEach((args, i) => {
    s.label(`beat${i}`).emit(TIMER, PCD.PUSHNUMBER, 32, MODULUS, PCD.PUSHNUMBER, 16, PCD.EQ).jump(PCD.IFGOTO, `go${i}`)
      .emit(PCD.DELAYDIRECT, 1).jump(PCD.GOTO, `beat${i}`).label(`go${i}`)
      .emit(...callFunc(SET_SECTOR_DAMAGE, args), PCD.DROP, PCD.DELAYDIRECT, 32, PLAYERHEALTH, PCD.ASSIGNSCRIPTVAR, i);
  });
  s.emit(PCD.BEGINPRINT, PCD.PUSHNUMBER, S.sectordamage, PCD.PRINTSTRING,
    ...steps.flatMap((_, i) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, PCD.PUSHSCRIPTVAR, i, PCD.PRINTNUMBER]));
  return s.emit(PCD.ENDLOG, PCD.TERMINATE);
}
// [Doruk-Aksoy] 10 fire damage, then none; then an unknown type twice, given UZDoom's interval and leakiness, which ours leaves out, then none.
export const SHARED_STEPS = [[ROOM_TAG, 10, S.Fire], [ROOM_TAG, 0]];
export const OURS_STEPS = [[ROOM_TAG, 10, S.ZxPlasma, 1, 256], [ROOM_TAG, 10, S.ZxPlasma, 1, 256], [ROOM_TAG, 0]];

// [Doruk-Aksoy] `withScripts` changes the script list and `withMap` the map, as netacsparity's CLIENTSIDE copies and player starts do.
export function buildAcsParityPk3({ withScripts = (s) => s, withMap = () => {} } = {}) {
  const m = new UdmfMap();
  const room = m.room(0, 0, 512, 512, { ...ROOM, id: ROOM_TAG }, (edge) => ({ side: { texturemiddle: "STARTAN2" }, line: edge === "south" ? { id: SOUTH_LINE } : {} }));
  // [Doruk-Aksoy] A pool in the far corner, out of the proximity checks' reach: a swimmable 3D floor to 48 from a control sector off the map, its floor ZxIce by UDMF's floorterrain.
  m.block(POOL.x0, POOL.y0, POOL.x1, POOL.y1, { ...ROOM, texturefloor: "FLAT14", id: POOL_TAG, floorterrain: "ZxIce" }, room);
  m.room(3200, 0, 3264, 64, { ...ROOM, heightfloor: 0, heightceiling: POOL.top, textureceiling: "FWATER1" }, (edge) => ({ side: { texturemiddle: "COMPBLUE" },
    line: edge === "west" ? { special: SPECIAL.Sector_Set3DFloor, arg0: POOL_TAG, arg1: 2, arg2: 0, arg3: 128 } : {} }));
  const first = m.lines.length;
  m.shape(DIAGONAL, { ...ROOM, texturefloor: MUD_FLAT, id: MUD_TAG }, 0);
  m.lines[first].id = DIAGONAL_LINE;
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 128, angle: 0, type: THING.ExplosiveBarrel, id: BARREL.near });
  m.thing({ x: 160, y: 330, angle: 0, type: THING.ExplosiveBarrel, id: BARREL.mud });
  m.thing({ x: 420, y: 64, angle: 0, type: THING.ExplosiveBarrel, id: BARREL.far });
  m.thing({ x: BARREL_AT.wet[0], y: BARREL_AT.wet[1], angle: 0, type: THING.ExplosiveBarrel, id: BARREL.wet });
  // [Doruk-Aksoy] One only a server's script recolours, which netacsparity reads on every client.
  m.thing({ x: BARREL_AT.net[0], y: BARREL_AT.net[1], angle: 0, type: THING.ExplosiveBarrel, id: BARREL.net });
  withMap(m);
  // [Doruk-Aksoy] Scripts with jump labels go first, each starting where the ones before end.
  const s3 = sectorDamageScript(SHARED_STEPS), w3 = s3.words(0), w4 = sectorDamageScript(OURS_STEPS).words(w3.length);
  const scripts = [{ code: w3, type: SCRIPT.CLOSED, number: 3 }, { code: w4, type: SCRIPT.CLOSED, number: 4 },
    { code: buildScript(), type: SCRIPT.CLOSED, number: 1 }, { code: buildNullDamageScript(), type: SCRIPT.CLOSED, number: 2 },
    { code: [PCD.PUSHNUMBER, 1, PCD.ASSIGNMAPVAR, 0, PCD.TERMINATE], type: SCRIPT.CLOSED, name: "zxstrarg" }];
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(withScripts(scripts), STRINGS) })) },
    { name: "terrain.txt", data: Buffer.from(TERRAIN, "latin1") }]);
}

// [Doruk-Aksoy] Each check's logged value, a string's without its < >, or null when script 1 logged no line.
export function parseParity(log) {
  const line = /parity((?: (?:<[^>]*>|-?\d+))+)/.exec(String(log));
  if (!line) return null;
  return [...line[1].matchAll(/<([^>]*)>|(-?\d+)/g)].map((t) => (t[1] !== undefined ? t[1] : Number(t[2])));
}

// [Doruk-Aksoy] What is wrong with the log against each check's answer, `engine` "uzdoom" taking UZDoom's own where it differs.
export function judgeParity(log, engine = "ours") {
  const got = parseParity(log);
  if (!got) return [`script 1 logged no checks (${String(log).trim().slice(0, 120)})`];
  if (got.length !== CHECKS.length) return [`script 1 logged ${got.length} values, not ${CHECKS.length}`];
  return CHECKS.flatMap((c, i) => {
    const want = engine === "uzdoom" && "uzdoom" in c ? c.uzdoom : c.want;
    return got[i] === want ? [] : [`${c.what} is ${JSON.stringify(got[i])}, not ${JSON.stringify(want)}`];
  });
}

// [Doruk-Aksoy] What is wrong with script 2's log: DamageActor on a missing tid gives P_DamageMobj's -1.
export function judgeNullDamage(log) {
  const m = /damagenull (-?\d+)/.exec(String(log));
  if (!m) return [`script 2 logged nothing (${String(log).trim().slice(0, 120)})`];
  return Number(m[1]) === -1 ? [] : [`DamageActor on a missing tid gave ${m[1]}, not -1`];
}

export const UNKNOWN_TYPE_WARNING = /SetSectorDamage: sectors here hold only the built-in damage types, so 'ZxPlasma' does normal damage/g;

// [Doruk-Aksoy] The healths a sector damage script logged, or null.
export function sectorHealths(log) {
  const m = /sectordamage((?: -?\d+)+)/.exec(String(log));
  return m ? m[1].trim().split(" ").map(Number) : null;
}

// [Doruk-Aksoy] What is wrong with scripts 3 and 4: one hurt tic of 10 a beat while set, none once cleared, and the unknown type's one warning.
export function judgeSectorDamage(shared, ours) {
  const faults = [];
  const a = sectorHealths(shared), b = sectorHealths(ours);
  if (!a || a.join() !== "90,90") faults.push(`script 3 logged healths ${a}, not 90,90`);
  if (!b || b.join() !== "80,70,70") faults.push(`script 4 logged healths ${b}, not 80,70,70`);
  const warnings = (String(ours).match(UNKNOWN_TYPE_WARNING) || []).length;
  if (warnings !== 1) faults.push(`an unknown damage type warned ${warnings} times, not once`);
  return faults;
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = judgeParity(await cap.exec(c, "puke 1", { quietMs: 400 }));
  faults.push(...judgeTranslations((await c.rpc("world.actors", {}))?.actors));
  faults.push(...judgeNullDamage(await cap.exec(c, "puke 2", { quietMs: 300 })));
  // [Doruk-Aksoy] The sandbox makes the player a god, which no sector can hurt.
  for (let i = 0; i < 2 && !/(God|Degreelessness) mode off/i.test(await cap.exec(c, "god", { quietMs: 200 })); i++);
  // [Doruk-Aksoy] Each step waits at most a beat to line up and a beat to hurt, so the log is read once three steps could have run.
  const run = async (n) => {
    const heard = cap.listen(c);
    await cap.exec(c, `puke ${n}`, { quietMs: 100 });
    await cap.waitTics(c, 64 * 3 + 20);
    heard.off();
    return heard.text();
  };
  faults.push(...judgeSectorDamage(await run(3), await run(4)));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${CHECKS.length} checks answer as UZDoom's (Ceil keeps a whole number), SetTranslation's barrels hold their translations, DamageActor on a missing tid gives -1, SetSectorDamage hurts once a beat and stops when cleared`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

// [Doruk-Aksoy] What is wrong with UZDoom's log: script 1 against UZDoom's answers, and script 3 hurting as ours does.
export function judgeUzdoom(log) {
  const healths = sectorHealths(log);
  return judgeParity(log, "uzdoom").concat(healths && healths.join() === "90,90" ? [] : [`SetSectorDamage logged healths ${healths}, not 90,90`]);
}

// [Doruk-Aksoy] The UZDoom side: the same pk3, scripts 1 and 3 puked, never script 2 (UZDoom's P_DamageMobj reads a null target's class and crashes).
export const UZDOOM_CHAIN = "wait 35;puke 1;wait 35;puke 3;wait 210;quit";

if (process.argv.includes("--uzdoom") && process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "acsparity-uzdoom-"));
  const pk3File = path.join(dir, "acsparity.pk3");
  fs.writeFileSync(pk3File, buildAcsParityPk3());
  const faults = judgeUzdoom(runUzdoom({ pk3: pk3File, map: "MAP01", chain: UZDOOM_CHAIN, dir }));
  console.log(faults.length ? `FAIL UZDoom: ${faults.join("; ")}` : `PASS UZDoom: ${CHECKS.length} checks give UZDoom's answers and SetSectorDamage hurts as ours does (${dir})`);
  process.exit(faults.length ? 1 : 0);
} else {
  runScenarioIfMain(import.meta.url, () => ({ name: "acsparity", wad: buildAcsParityPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
}
