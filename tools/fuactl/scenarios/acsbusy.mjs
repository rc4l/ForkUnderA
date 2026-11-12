// [rc4l] An ACS script flagged BUSY runs past the runaway limit to its end, and an unflagged script running as long is still terminated.
//
// usage:  node scenarios/acsbusy.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, addToCVar, PCD, SCRIPTF } from "../src/acs.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

// [rc4l] The scripts' one string, the counter each adds its bits to.
export const STRINGS = ["zx_acsbusy_passed"];
const PASSED = 0;
// [rc4l] The busy script's bit on reaching its end, the plain one's on starting, and the plain one's on reaching its end, which it must not.
export const BITS = { busyEnded: 1, plainEnded: 2, plainRan: 4 };
// [rc4l] Loop passes of five p-codes each, so a script runs 2.5 times the runaway limit of 2000000 in one tic.
export const PASSES = 1000000;

// [rc4l] The words of a loop counting map variable `mapVar` up to PASSES in one tic, starting at word `at` of the lump.
export function busyLoop(mapVar, at, before = [], after = []) {
  const s = new AcsScript().emit(...before);
  s.label("loop").emit(PCD.INCMAPVAR, mapVar, PCD.PUSHMAPVAR, mapVar, PCD.PUSHNUMBER, PASSES, PCD.GE).jump(PCD.IFNOTGOTO, "loop");
  s.emit(...after, PCD.TERMINATE);
  return s.words(at);
}

// [rc4l] Script 1, flagged BUSY, and script 2, unflagged, the second's code laid after the first's.
export function buildScripts() {
  const busy = busyLoop(0, 0, [], addToCVar(PASSED, BITS.busyEnded));
  const plain = busyLoop(1, busy.length, addToCVar(PASSED, BITS.plainRan), addToCVar(PASSED, BITS.plainEnded));
  return [{ code: busy, number: 1, flags: SCRIPTF.BUSY }, { code: plain, number: 2 }];
}

export function buildAcsBusyPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(buildScripts(), STRINGS) })) },
    { name: "cvarinfo.txt", data: "server int zx_acsbusy_passed = 0;\n" },
  ]);
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] Exactly the busy script's end and the plain script's start: 5.
const EXPECTED = BITS.busyEnded + BITS.plainRan;
export const COUNTER = { command: "zx_acsbusy_passed", label: "scripts' bits (busy ended 1, plain ended 2, plain ran 4)",
  pattern: "\"zx_acsbusy_passed\" is \"(\\d+)\"", floor: EXPECTED, ceiling: EXPECTED };

export const SIDES = [{ name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "acsbusy", wad: buildAcsBusyPk3(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES }), compareCounter);
