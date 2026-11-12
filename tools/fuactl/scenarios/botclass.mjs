// [rc4l] A bot added to a game of one's own spawns as the class its BOTINFO names (zandronum@6ee2a204c6b5): exit 0 when the bot's pawn is that class, where it came in as the first player class.
//
// usage:  node scenarios/botclass.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const CLASS = "ZXPlayer", BOT = "ZXBot";

export const DECORATE = `actor ${CLASS} : DoomPlayer\n{\n  Player.DisplayName "${CLASS}"\n}\n`;
// [rc4l] The bot's class second, so a bot that ignores it comes in as the first.
export const MAPINFO = `gameinfo\n{\n  playerclasses = "DoomPlayer", "${CLASS}"\n}\n\nmap MAP01 "Bots"\n{\n}\n`;
export const BOTINFO = `{\n  name = "${BOT}"\n  class = "${CLASS}"\n  revealed = true\n}\n`;

export function buildBotClassPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: MAPINFO },
    { name: "botinfo.txt", data: BOTINFO },
  ]);
}

// [rc4l] What is wrong with the pawns the players own once the bot is in: one for the bot, of its class.
export function judgePawns(pawns) {
  const bots = (pawns || []).filter((a) => a.player > 0);
  if (bots.length !== 1) return [`${bots.length} bot pawns, not 1`];
  return bots[0].c === CLASS ? [] : [`the bot's pawn is ${bots[0].c}, not ${CLASS}`];
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  await cap.exec(c, `addbot ${BOT}`);
  await cap.waitTics(c, 35);
  const faults = judgePawns((await c.rpc("world.actors", { players: true, all: true }))?.actors);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${BOT} spawned as ${CLASS}`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "botclass", wad: buildBotClassPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
