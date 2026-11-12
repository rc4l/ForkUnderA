// [rc4l] ACS's RemoveBot on a bot whose BOTINFO name carries colour codes (zandronum@9bd2e71f1476, zandronum@7b5866e1922c): exit 0 when its plain name, an empty name and its coloured name each take the bot out of the game.
//
// usage:  node scenarios/botremove.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const BOT = "ZXBot";
// [rc4l] The bot's name with a colour code before each half, as a mod colours its bots.
export const COLOURED = "\\cgZX\\cdBot";
// [rc4l] ACS's RemoveBot, the ACSF after AddBot, which acs.mjs has no name for yet.
export const REMOVE_BOT = 169;
export const BOTINFO = `{\n  name = "${COLOURED}"\n  revealed = true\n}\n`;
// [rc4l] Each name a script passes, the coloured one with the escape byte ACC compiles \c to, as StrParam's n: gives it.
export const CASES = [
  { script: 1, what: "the plain name", text: BOT },
  { script: 2, what: "an empty name", text: "" },
  { script: 3, what: "the coloured name", text: "\x1cgZX\x1cdBot" },
];

// [rc4l] A script calling RemoveBot with string `string`.
export function buildScript(string) {
  return [PCD.PUSHNUMBER, string, PCD.CALLFUNC, 1, REMOVE_BOT, PCD.DROP, PCD.TERMINATE];
}

export function buildBotRemovePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  const scripts = CASES.map((c, i) => ({ code: buildScript(i), type: SCRIPT.CLOSED, number: c.script }));
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, CASES.map((c) => c.text)) })) },
    { name: "botinfo.txt", data: BOTINFO },
  ]);
}

// [rc4l] The players' pawns that are not the console player's, which are the bots.
const botCount = async (c) => ((await c.rpc("world.actors", { players: true, all: true }))?.actors || []).filter((a) => a.player > 0).length;

// [rc4l] What is wrong with one case: a bot joined, and the script took it out.
export function judgeCase(what, before, after) {
  if (before !== 1) return [`addbot left ${before} bots, not 1, before ${what}`];
  return after === 0 ? [] : [`RemoveBot given ${what} left ${after} bots`];
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = [];
  for (const k of CASES) {
    await cap.exec(c, `addbot ${BOT}`);
    await cap.waitTics(c, 35);
    const before = await botCount(c);
    await cap.exec(c, `puke ${k.script}`);
    await cap.waitTics(c, 35);
    faults.push(...judgeCase(k.what, before, await botCount(c)));
    // [rc4l] A bot a case left behind must not count against the next.
    await cap.exec(c, "removebots");
    await cap.waitTics(c, 10);
  }
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 } : { line: `PASS ${label}: RemoveBot took ${COLOURED} out by ${CASES.map((k) => k.what).join(", ")}`, failed: 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "botremove", wad: buildBotRemovePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
