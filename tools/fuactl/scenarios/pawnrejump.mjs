// [rc4l] A player pawn nobody plays, given a RejumpDelay and pushed up onto a step while still rising, which zandronum@f6f2e6ee1991 crashed on by giving its missing player a jump wait; exit 0 when the engine lives and the pawn stands on the step.
//
// usage:  node scenarios/pawnrejump.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, addToCVar, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";
import { APROP_REJUMPDELAY } from "./netrejumpdelay.mjs";

export const COUNTER_CVAR = "zx_pawnrejump_wrong";
const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
// [rc4l] The step the pawn climbs, low enough to step onto and starting just past its east edge.
export const STEP = { x0: 512, x1: 768, height: 16 };
// [rc4l] The pawn spawns on the floor west of the step, since a pawn nobody plays is put on the floor as it begins, then moves onto it while rising slower than the step is high.
export const PAWN = { x: 490, y: 128, tid: 10, velx: 12, velz: 4 };
export const PLAYER = { x: 64, y: 128 };
const FIXED = 65536;
const STRINGS = [COUNTER_CVAR, "DoomPlayer"];

// [rc4l] Two checks and one for the puke itself, so a puke that never ran reads 0 and a pass reads 1.
export const CHECKS = 2;

export function buildScript() {
  return new AcsScript()
    .emit(...addToCVar(0, CHECKS + 1))
    .emit(...callFunc(ACSF.SpawnForced, [1, PAWN.x * FIXED, PAWN.y * FIXED, 0, PAWN.tid, 0]), PCD.DROP)
    .emit(PCD.PUSHNUMBER, PAWN.tid, PCD.PUSHNUMBER, APROP_REJUMPDELAY, PCD.PUSHNUMBER, 0, PCD.SETACTORPROPERTY)
    .emit(PCD.DELAYDIRECT, 2)
    .emit(...callFunc(ACSF.SetActorVelocity, [PAWN.tid, PAWN.velx * FIXED, 0, PAWN.velz * FIXED, 0, 0]), PCD.DROP)
    .emit(PCD.DELAYDIRECT, 35)
    .emit(...callFunc(ACSF.CheckActorClass, [PAWN.tid, 1])).jump(PCD.IFNOTGOTO, "height")
    .emit(...addToCVar(0, -1))
    .label("height").emit(PCD.PUSHNUMBER, PAWN.tid, PCD.GETACTORZ, PCD.PUSHNUMBER, STEP.height * FIXED, PCD.GE).jump(PCD.IFNOTGOTO, "done")
    .emit(...addToCVar(0, -1))
    .label("done").emit(PCD.TERMINATE)
    .words();
}

export function buildMap() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 256, ROOM, () => ({ side: WALL }));
  m.block(STEP.x0, 32, STEP.x1, 224, { ...ROOM, heightfloor: STEP.height }, room, () => ({ texturebottom: "STARTAN2" }));
  m.thing({ ...PLAYER, angle: 0, type: 1 });
  return m;
}

export function buildPawnRejumpPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap(), { behavior: acsScripts([{ code: buildScript(), open: false, number: 1 }], STRINGS) })) },
    { name: "mapinfo.txt", data: `map MAP01 "Playerless pawn"\n{\n}\n` },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const COUNTER = { command: COUNTER_CVAR, label: "checks the playerless pawn failed, gone or off the step, plus the puke's own one",
  pattern: `"${COUNTER_CVAR}" is "(-?\\d+)"`, ceiling: 1, floor: 1 };

runScenarioIfMain(import.meta.url, () => ({ name: "pawnrejump", wad: buildPawnRejumpPk3(), maps: ["MAP01"],
  vantages: [{ name: "west", at: { ...PLAYER, z: 0, angle: 0, pitch: 0 } }], counter: COUNTER,
  sides: [{ name: "playerless", launchCvars: { ...MUTE }, commands: ["puke 1"] }], holdTics: 70 }), compareCounter);
