// [rc4l] A bot on a server that loads a new level without an intermission (zandronum@eb422df3b1be): exit 0 when the server builds the bot's nodes on the new level and lives, where building them before the level had nodes crashed it.
//
// usage:  node scenarios/netbotmapchange.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const MAPS = ["MAP01", "MAP02"];
// [rc4l] MAP01 ends straight into MAP02, so the bot is still in the game while the new level loads.
export const MAPINFO = `map MAP01 "one"
{
	next = "MAP02"
	nointermission
}
map MAP02 "two"
{
}
`;

export function buildNetBotMapChangePk3() {
  return pk3(MAPS.map((name, i) => {
    const m = new UdmfMap();
    // [rc4l] A raised step on the second map, so a node's floor height is not the same everywhere.
    m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
    if (i === 1) m.room(512, 0, 1024, 512, { ...ROOM, heightfloor: 24 }, () => ({ side: { texturemiddle: "STARTAN2", texturebottom: "STARTAN2" } }));
    for (const [type, x] of [[1, 128], [2, 256], [3, 320], [11, 192]]) m.thing({ x, y: 64, angle: 90, type });
    return { name: `maps/${name}.wad`, data: wad(mapLumps(name, m)) };
  }).concat([{ name: "mapinfo.txt", data: MAPINFO }]));
}

// [rc4l] What is wrong once the server was asked to load MAP02 with a bot in the game.
export function judgeMapChange({ alive, map, bots }) {
  const faults = [];
  if (!alive) return ["the server died loading MAP02 with a bot in the game"];
  if (map !== "MAP02") faults.push(`the server is on ${map}, not MAP02`);
  if (!(bots >= 1)) faults.push(`the server has ${bots} bots after the change`);
  return faults;
}

export async function judgeNetplay(set, label) {
  const server = set.server.c;
  await cap.exec(server, "addbot");
  await cap.waitTics(server, 70);
  await cap.exec(server, "changemap MAP02");
  let alive = true, map = null, bots = 0;
  try {
    await cap.waitTics(server, 105, { timeoutMs: 20000 });
    const players = (await server.rpc("world.actors", { players: true }))?.actors || [];
    map = String((await server.rpc("sim.tic", {}))?.map || "").toUpperCase();
    // [rc4l] Every pawn a player owns but the joined client's.
    bots = players.filter((a) => a.player >= 0).length - 1;
  } catch {
    alive = false;
  }
  const faults = judgeMapChange({ alive, map, bots });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the server loaded MAP02 with its bot`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "bot", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netbotmapchange", wad: buildNetBotMapChangePk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
