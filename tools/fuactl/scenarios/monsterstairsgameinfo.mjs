// [Doruk-Aksoy] monsterstairs.mjs's climb, chosen by a mod's MAPINFO gameinfo block (fua_monsterstairs = true) with sv_fua_monsterstairs left off: exit 0 when every walker climbs the flight.
//
// usage:  node scenarios/monsterstairsgameinfo.mjs [port]
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";
import { buildMonsterStairsPk3, VANTAGES, COUNTER, HOLD_TICS, SPAWN_WALKERS } from "./monsterstairs.mjs";

export const GAMEINFO = ["fua_monsterstairs = true"];

export const SIDES = [{ name: "gameinfo", launchCvars: { ...MUTE, sv_fua_monsterstairs: 0 }, commands: SPAWN_WALKERS }];

runScenarioIfMain(import.meta.url, () => ({ name: "monsterstairsgameinfo", wad: buildMonsterStairsPk3(["MAP01"], GAMEINFO),
  maps: ["MAP01"], vantages: VANTAGES, counter: COUNTER, sides: SIDES, holdTics: HOLD_TICS }), compareCounter);
