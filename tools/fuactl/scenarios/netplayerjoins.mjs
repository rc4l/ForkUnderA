// [rc4l] GAMEEVENT_PLAYERJOINS on a server (zandronum@96f065639407): exit 0 when an EVENT script is asked as a client joins, with its player, and a result of zero keeps the client out until the script lets it in.
//
// usage:  node scenarios/netplayerjoins.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, addToCVar, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { clientPlaying, joinGame } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { scaled } from "../src/timeouts.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] GAMEEVENT_PLAYERJOINS in gamemode.h.
export const GAMEEVENT_PLAYERJOINS = 20;
// [rc4l] How often the event was asked, the player it last named, and the switch that makes it refuse.
export const STRINGS = ["zx_joins_asked", "zx_joins_player", "zx_joins_refuse"];
const ASKED = 0, PLAYER = 1, REFUSE = 2;

// [rc4l] EVENT (type, player, team): on PLAYERJOINS count the ask and keep the player, then result 0 while zx_joins_refuse is set.
export function buildScript() {
  const s = new AcsScript();
  s.emit(PCD.PUSHSCRIPTVAR, 0, PCD.PUSHNUMBER, GAMEEVENT_PLAYERJOINS, PCD.EQ).jump(PCD.IFNOTGOTO, "end");
  s.emit(...addToCVar(ASKED, 1), PCD.PUSHNUMBER, PLAYER, PCD.PUSHSCRIPTVAR, 1, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP);
  s.emit(PCD.PUSHNUMBER, REFUSE, PCD.GETCVAR).jump(PCD.IFNOTGOTO, "end");
  s.emit(PCD.PUSHNUMBER, 0, PCD.SETRESULTVALUE);
  s.label("end").emit(PCD.TERMINATE);
  return s.words();
}

export function buildNetPlayerJoinsPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.EVENT, args: 3 }], STRINGS) })) },
    { name: "cvarinfo.txt", data: STRINGS.map((name) => `server int ${name} = 0;\n`).join("") },
  ]);
}

// [rc4l] What is wrong with the event's record and the client's place through the three steps.
export function judgeJoins({ player, askedAtJoin, namedAtJoin, askedRefused, playingRefused, playingAllowed }) {
  const faults = [];
  if (!(askedAtJoin >= 1)) faults.push(`the event was asked ${askedAtJoin} times as the client joined`);
  else if (namedAtJoin !== player) faults.push(`the event named player ${namedAtJoin}, not ${player}`);
  if (!(askedRefused > askedAtJoin)) faults.push("the event was not asked again on the refused join");
  if (playingRefused) faults.push("the client joined with the event's result at zero");
  if (!playingAllowed) faults.push("the client never joined once the event allowed it");
  return faults;
}

const cvarOf = async (c, name) => Number(/is "(-?\d+)"/.exec(String(await cap.exec(c, name)))?.[1]);

export async function judgeNetplay(set, label) {
  const server = set.server.c, client = set.clients[0];
  await cap.waitTics(server, 10);
  const askedAtJoin = await cvarOf(server, STRINGS[ASKED]);
  const namedAtJoin = await cvarOf(server, STRINGS[PLAYER]);
  await cap.exec(server, `${STRINGS[REFUSE]} 1`);
  await cap.exec(client.c, "spectate");
  // [rc4l] The server takes no second join within three seconds of the last.
  await cap.sleep(3500);
  await cap.exec(client.c, "join");
  await cap.waitTics(server, 70);
  const askedRefused = await cvarOf(server, STRINGS[ASKED]);
  const playingRefused = clientPlaying(await server.rpc("net.state", {}), client.player);
  await cap.exec(server, `${STRINGS[REFUSE]} 0`);
  await cap.exec(client.c, "spectate");
  await cap.sleep(3500);
  const { joined } = await joinGame(client, server, { timeoutMs: scaled(20000) });
  const faults = judgeJoins({ player: client.player, askedAtJoin, namedAtJoin, askedRefused, playingRefused, playingAllowed: joined });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the event named player ${client.player}, kept them out at zero and let them in after`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "joins", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netplayerjoins", wad: buildNetPlayerJoinsPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
