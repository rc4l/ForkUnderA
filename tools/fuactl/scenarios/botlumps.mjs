// [MGOOOOOO] A bot added from a tic on the game thread, with a script and a chat lump, reads neither lump off the main thread: exit 0 when fua_seams lists no wad/read entry by another thread, since an archive has one reader and such a read beside a frame's reads corrupts both.
//
// usage:  node scenarios/botlumps.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { parseSeamReport } from "../src/seams.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const BOT = "ZXChatter";
// [MGOOOOOO] The stock bot script and a stock chat lump, the bot chatting at every chance it gets.
export const BOTINFO = `{\n  name = "${BOT}"\n  script = "humanbot"\n  chatlump = "bots/chatfiles/soldier.txt"\n  chatfrequency = 100\n  revealed = true\n}\n`;
// [MGOOOOOO] The script that adds the bot, run by puke so that it runs in a tic.
export const ADD_SCRIPT = 1;

export function buildBotLumpsPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [11, 512], [11, 896]]) m.thing({ x, y: 512, angle: 0, type });
  const scripts = [{ code: [...callFunc(ACSF.AddBot, [0]), PCD.DROP, PCD.TERMINATE], type: SCRIPT.CLOSED, number: ADD_SCRIPT }];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, [BOT]) })) },
    { name: "botinfo.txt", data: BOTINFO },
  ]);
}

// [MGOOOOOO] What is wrong with the seam report: unlisted, or a wad/read site entered by a thread other than the main one.
export function judgeReport(r) {
  if (!r.found) return ["fua_seams listed nothing"];
  return r.sites.filter((s) => s.startsWith("wad/read")).map((s) => `read a lump off the main thread: ${s}`);
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  // [MGOOOOOO] The report clears what it printed, so the level's start is not judged.
  await cap.exec(c, "fua_seams", { maxMs: 6000 });
  await cap.exec(c, `puke ${ADD_SCRIPT}`);
  await cap.waitTics(c, 175);
  const faults = judgeReport(parseSeamReport(await cap.exec(c, "fua_seams", { quietMs: 400, maxMs: 6000 })));
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 } : { line: `PASS ${label}: ${BOT} joined from a tic and played 5 seconds with no lump read off the main thread`, failed: 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "botlumps", wad: buildBotLumpsPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
