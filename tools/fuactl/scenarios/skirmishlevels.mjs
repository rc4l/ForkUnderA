// [rc4l] A map MAPINFO marks noskirmish (zandronum@b417096783c1): exit 0 when the skirmish menu's level list goes from MAP01 to MAP03, past the map between them.
//
// usage:  node scenarios/skirmishlevels.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { readMenu, menuNav } from "../src/ui.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const MAPINFO = `map MAP01 "One"\n{\n}\n\nmap MAP02 "Two"\n{\n  noskirmish\n}\n\nmap MAP03 "Three"\n{\n}\n`;
// [rc4l] Steps the level option may take looking for MAP01, more than the iwad's maps.
export const MAX_STEPS = 40;

export function buildSkirmishLevelsPk3() {
  const room = () => { const m = new UdmfMap(); m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } })); m.thing({ x: 256, y: 256, angle: 90, type: 1 }); return m; };
  return pk3([
    ...["MAP01", "MAP02", "MAP03"].map((name) => ({ name: `maps/${name}.wad`, data: wad(mapLumps(name, room())) })),
    { name: "mapinfo.txt", data: MAPINFO },
  ]);
}

// [rc4l] The map the menu's level row shows, from the text drawn on it.
export function shownLevel(lines) {
  const row = (lines || []).find((l) => /\bMAP\d\d - /.test(l.text));
  return row ? /\b(MAP\d\d) - /.exec(row.text)[1] : null;
}

// [rc4l] What is wrong with the level after MAP01.
export function judgeNext(next) {
  if (next === null) return ["the menu showed no level after MAP01"];
  return next === "MAP03" ? [] : [`the level after MAP01 is ${next}, not MAP03`];
}

export async function judgeLaunch(c, inst, label) {
  await cap.exec(c, "openmenu ZA_SkirmishMenu");
  let shown = shownLevel((await readMenu(c)).lines);
  for (let i = 0; shown !== "MAP01" && i < MAX_STEPS; i++) {
    await menuNav(c, ["left"]);
    shown = shownLevel((await readMenu(c)).lines);
  }
  if (shown !== "MAP01") return { line: `FAIL ${label}: the level row never showed MAP01 (last ${shown})`, failed: 1 };
  await menuNav(c, ["right"]);
  const next = shownLevel((await readMenu(c)).lines);
  const faults = judgeNext(next);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: MAP01 then ${next}`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "skirmishlevels", wad: buildSkirmishLevelsPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
