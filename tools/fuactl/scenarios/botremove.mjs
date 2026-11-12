// [rc4l] ACS's RemoveBot on a bot whose BOTINFO name carries colour codes (zandronum@9bd2e71f1476, zandronum@7b5866e1922c, zandronum@eb1ce25f51dc): exit 0 when its plain, empty and coloured names each take the bot out, and a player index takes it out only when it is the bot's.
//
// usage:  node scenarios/botremove.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const BOT = "ZXBot";
// [rc4l] The bot's name with a colour code before each half, as a mod colours its bots.
export const COLOURED = "\\cgZX\\cdBot";
export const BOTINFO = `{\n  name = "${COLOURED}"\n  revealed = true\n}\n`;
// [rc4l] The names a script passes, the coloured one with the escape byte ACC compiles \c to, as StrParam's n: gives it.
export const STRINGS = [BOT, "", "\x1cgZX\x1cdBot"];
// [rc4l] The bot's player index, the first after the console player's.
export const BOT_INDEX = 1;
// [rc4l] Each script's RemoveBot arguments, a string's index then any player index, and whether the bot stays.
export const CASES = [
  { script: 1, what: "the plain name", args: [0] },
  { script: 2, what: "an empty name", args: [1] },
  { script: 3, what: "the coloured name", args: [2] },
  { script: 4, what: "the name and its index", args: [0, BOT_INDEX] },
  { script: 5, what: "the name and another index", args: [0, BOT_INDEX + 1], stays: true },
  { script: 6, what: "the name and an index past the players", args: [0, 64], stays: true },
  { script: 7, what: "an empty name and its index", args: [1, BOT_INDEX] },
  { script: 8, what: "an empty name and the console player's index", args: [1, 0], stays: true },
];

// [rc4l] A script calling RemoveBot with `args`.
export function buildScript(args) {
  return [...callFunc(ACSF.RemoveBot, args), PCD.DROP, PCD.TERMINATE];
}

export function buildBotRemovePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  const scripts = CASES.map((c) => ({ code: buildScript(c.args), type: SCRIPT.CLOSED, number: c.script }));
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS) })) },
    { name: "botinfo.txt", data: BOTINFO },
  ]);
}

// [rc4l] The players' pawns that are not the console player's, which are the bots.
const botCount = async (c) => ((await c.rpc("world.actors", { players: true, all: true }))?.actors || []).filter((a) => a.player > 0).length;

// [rc4l] What is wrong with one case: a bot joined, and the script took it out or, where it `stays`, left it.
export function judgeCase(what, before, after, stays = false) {
  if (before !== 1) return [`addbot left ${before} bots, not 1, before ${what}`];
  if (stays) return after === 1 ? [] : [`RemoveBot given ${what} took the bot out`];
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
    faults.push(...judgeCase(k.what, before, await botCount(c), k.stays));
    // [rc4l] A bot a case left behind must not count against the next.
    await cap.exec(c, "removebots");
    await cap.waitTics(c, 10);
  }
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 } : { line: `PASS ${label}: RemoveBot took ${COLOURED} out by ${CASES.filter((k) => !k.stays).map((k) => k.what).join(", ")}, and left it by ${CASES.filter((k) => k.stays).map((k) => k.what).join(", ")}`, failed: 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "botremove", wad: buildBotRemovePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
