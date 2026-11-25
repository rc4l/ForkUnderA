// [Doruk-Aksoy] ACS function references between modules and across map changes, from fixtures/acsfuncref: exit 0 when a reference one library takes to another's function compares equal to that library's own,
// [Doruk-Aksoy] one kept in a global variable still calls and compares right after a map whose own BEHAVIOR moves every library's load position, and one into a library the new map never loads stops its script with a message instead of running another module's function.
//
// usage:  node scenarios/acsfuncref.mjs
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const fixture = (name) => fs.readFileSync(fileURLToPath(new URL(`../fixtures/acsfuncref/${name}.o`, import.meta.url)));
const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [Doruk-Aksoy] Every map ends in the next with no tally, and only MAP02 has a BEHAVIOR of its own.
export const MAPINFO = ["MAP01", "MAP02", "MAP03"].map((map) => `map ${map} "${map}"\n{\n\tnointermission\n}\n`).join("");
// [Doruk-Aksoy] The message PCD_CALLSTACK prints for a reference into a library the map does not load.
export const UNLOADED = /which this map does not load/;

function room() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 128, y: 64, angle: 90, type: 1 });
  return m;
}

export function buildAcsFuncRefPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room())) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room(), { behavior: fixture("map02") })) },
    { name: "maps/MAP03.wad", data: wad(mapLumps("MAP03", room())) },
    ...["freflib", "frefuse", "frefmap"].map((lib) => ({ name: `acs/${lib}.o`, data: fixture(lib) })),
    { name: "LOADACS.txt", data: "freflib\nfrefuse\n" },
    { name: "mapinfo.txt", data: MAPINFO },
  ]);
}

// [Doruk-Aksoy] The number a script logged after "FREF <what> ", or null when it logged none.
const logged = (text, what) => {
  const m = new RegExp(`FREF ${what} (-?\\d+)`).exec(String(text));
  return m ? Number(m[1]) : null;
};

// [Doruk-Aksoy] What is wrong with each step's console output: MAP01's script 901, MAP02's scripts 904 and 902, MAP03's script 903.
export function judgeSteps({ map01, map02map, map02, map03 }) {
  const faults = [];
  const want = (text, what, value, means) => {
    const got = logged(text, what);
    if (got !== value) faults.push(`${means}: logged ${got === null ? "nothing" : got}, not ${value}`);
  };
  want(map01, "same", 1, "a reference taken in another module compares equal to the library's own");
  want(map01, "call", 7, "the reference calls Seven");
  if (!/FREF mapstored/.test(String(map02map))) faults.push("MAP02's own script never ran");
  want(map02, "stored", 7, "a reference kept from MAP01 calls Seven on MAP02");
  want(map02, "storedsame", 1, "a reference kept from MAP01 compares equal on MAP02");
  if (!/FREF reached/.test(String(map03))) faults.push("MAP03's script never ran");
  const ran = logged(map03, "unloaded");
  if (ran !== null) faults.push(`a reference into a library MAP03 does not load ran a function, which returned ${ran}`);
  if (!UNLOADED.test(String(map03))) faults.push(`calling into a library MAP03 does not load printed no message (${String(map03).trim().slice(-120)})`);
  return faults;
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE } }];

// [Doruk-Aksoy] Runs each map's scripts in turn, changing maps by changemap so global variables carry over.
export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  const next = async (map) => {
    await exec(`changemap ${map}`);
    await cap.waitInLevel(c, undefined, { pressThrough: true });
    await cap.waitTics(c, 5);
  };
  await cap.waitTics(c, 10);
  const map01 = await exec("puke 901");
  await next("MAP02");
  const map02map = await exec("puke 904");
  const map02 = await exec("puke 902");
  await next("MAP03");
  const map03 = await exec("puke 903");
  const faults = judgeSteps({ map01, map02map, map02, map03 });
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: references compare equal between modules, survive two map changes, and one into an unloaded library stops its script`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsfuncref", wad: buildAcsFuncRefPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
