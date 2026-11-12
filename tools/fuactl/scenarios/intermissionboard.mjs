// [rc4l] The intermission's scoreboard under a whole-screen title graphic, as Eviternity's (zandronum@465e1c524db5): exit 0 when the board's heading is drawn in the screen's upper half, where it started below the graphic, at the bottom.
//
// usage:  node scenarios/intermissionboard.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acsScripts, PCD, SCRIPT, callSpecial } from "../src/acs.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { readMenu } from "../src/ui.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const TITLE = "ZXTITLE", MAGENTA = [255, 0, 255];
export const MAPINFO = `map MAP01 "Board"\n{\n  next = "MAP02"\n  titlepatch = "${TITLE}"\n}\n\nmap MAP02 "After"\n{\n}\n`;

// [rc4l] A 320x200 title, the whole of a clean screen.
export function titlePng() {
  const [w, h] = [320, 200];
  const rgb = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) rgb.set(MAGENTA, i * 3);
  return encodePNG(w, h, rgb);
}

export function buildIntermissionBoardPk3() {
  const room = () => { const m = new UdmfMap(); m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } })); m.thing({ x: 256, y: 64, angle: 90, type: 1 }); m.thing({ x: 256, y: 256, angle: 90, type: 11 }); return m; };
  const exit = [{ code: [...callSpecial(SPECIAL.Exit_Normal, [0]), PCD.TERMINATE], type: SCRIPT.CLOSED, number: 1 }];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room(), { behavior: acsScripts(exit, []) })) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room())) },
    { name: "mapinfo.txt", data: MAPINFO },
    { name: `graphics/${TITLE.toLowerCase()}.png`, data: titlePng() },
  ]);
}

// [rc4l] What is wrong with the board: its "Rankings" heading must be in the screen's upper half.
export function judgeBoard(lines, height) {
  const heading = lines.find((l) => /Rankings/.test(l.text));
  if (!heading) return ["the intermission drew no scoreboard"];
  return heading.y < height / 2 ? [] : [`the scoreboard starts at ${heading.y} of ${height}, below the title, not near the top`];
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  await cap.exec(c, "puke 1");
  await cap.waitTics(c, 20);
  const { height } = await c.rpc("input.windowpoint", { x: 0, y: 0 });
  const { lines } = await readMenu(c);
  const faults = judgeBoard(lines, height);
  const heading = lines.find((l) => /Rankings/.test(l.text));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the scoreboard starts at ${heading.y} of ${height}`, failed: faults.length ? 1 : 0 };
}

// [rc4l] A deathmatch, whose intermission draws the scoreboard.
export const SIDES = [{ name: "dm", launchCvars: { deathmatch: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "intermissionboard", wad: buildIntermissionBoardPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
