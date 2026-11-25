// [MGOOOOOO] A bot added from a tic on the game thread asks the main thread for no more than its script and the colour table: exit 0 when fua_gamethread_report counts at most MAX_ASKS calls across the join, since each ask holds the tic until the main thread's next frame (a second of frozen game per bot at 60 frames a second when every colour name asked).
//
// usage:  node scenarios/botasks.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { judgeAsks, readAsks } from "../src/asks.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const BOT = "ZXQuiet";
// [MGOOOOOO] The stock bot script, a colour by name, and no chat, so the join's only reads are the script and the colours.
export const BOTINFO = `{\n  name = "${BOT}"\n  script = "humanbot"\n  color = "dark green"\n  chatfrequency = 0\n  revealed = true\n}\n`;
export const ADD_SCRIPT = 1;
// [MGOOOOOO] The bot's script, and the colour table on the first colour name looked up in a tic.
export const MAX_ASKS = 2;

export function buildBotAsksPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [11, 512], [11, 896]]) m.thing({ x, y: 512, angle: 0, type });
  const scripts = [{ code: [...callFunc(ACSF.AddBot, [0]), PCD.DROP, PCD.TERMINATE], type: SCRIPT.CLOSED, number: ADD_SCRIPT }];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, [BOT]) })) },
    { name: "botinfo.txt", data: BOTINFO },
  ]);
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 35);
  const before = await readAsks(cap, c);
  await cap.exec(c, `puke ${ADD_SCRIPT}`);
  await cap.waitTics(c, 70);
  const after = await readAsks(cap, c);
  const faults = judgeAsks(before, after, MAX_ASKS);
  return faults.length ? { line: `FAIL ${label}: the join ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: ${BOT} joined from a tic asking the main thread ${after - before} times (at most ${MAX_ASKS})`, failed: 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "botasks", wad: buildBotAsksPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
