// [rc4l] ACS's RemoveBot given the plain name of a bot whose BOTINFO name carries colour codes (zandronum@9bd2e71f1476): exit 0 when the bot leaves the game.
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

// [rc4l] Script 1: RemoveBot("ZXBot").
export function buildScript() {
  return [PCD.PUSHNUMBER, 0, PCD.CALLFUNC, 1, REMOVE_BOT, PCD.DROP, PCD.TERMINATE];
}

export function buildBotRemovePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }], [BOT]) })) },
    { name: "botinfo.txt", data: BOTINFO },
  ]);
}

// [rc4l] The players' pawns that are not the console player's, which are the bots.
const botPawns = (actors) => (actors || []).filter((a) => a.player > 0);

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  await cap.exec(c, `addbot ${BOT}`);
  await cap.waitTics(c, 35);
  const before = botPawns((await c.rpc("world.actors", { players: true, all: true }))?.actors).length;
  if (before !== 1) return { line: `FAIL ${label}: addbot left ${before} bots, not 1`, failed: 1 };
  await cap.exec(c, "puke 1");
  await cap.waitTics(c, 35);
  const after = botPawns((await c.rpc("world.actors", { players: true, all: true }))?.actors).length;
  return after === 0 ? { line: `PASS ${label}: RemoveBot("${BOT}") removed ${COLOURED}`, failed: 0 } : { line: `FAIL ${label}: RemoveBot("${BOT}") left ${after} bots`, failed: 1 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "botremove", wad: buildBotRemovePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
