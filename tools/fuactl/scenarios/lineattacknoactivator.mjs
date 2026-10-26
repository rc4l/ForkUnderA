// [Doruk-Aksoy] An OPEN script, which has no activator, calling LineAttack(0, ...): exit 0 when the attack fires nothing and the script runs on, rather than P_LineAttack dereferencing the missing shooter.
//
// usage:  node scenarios/lineattacknoactivator.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acs0, callFunc, incrementCVar, ACSF, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] A few tics in, so the level is up; LineAttack's result is dropped, then the script counts that it got past it.
export const BEHAVIOR = acs0([PCD.DELAYDIRECT, 5, ...callFunc(ACSF.LineAttack, [0, 0, 0, 0]), PCD.DROP, ...incrementCVar(0), PCD.TERMINATE],
  ["zx_lineattack_survived"]);

export function buildLineAttackNoActivatorPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: BEHAVIOR })) },
    { name: "cvarinfo.txt", data: "server int zx_lineattack_survived = 0;\n" },
  ]);
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

export const COUNTER = { command: "zx_lineattack_survived", label: "scripts that ran past LineAttack",
  pattern: "\"zx_lineattack_survived\" is \"(\\d+)\"", floor: 1 };

export const SIDES = [{ name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "lineattacknoactivator", wad: buildLineAttackNoActivatorPk3(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES }), compareCounter);
