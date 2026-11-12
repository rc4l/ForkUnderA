// [rc4l] The server's flood rule for userinfo changes (zandronum@227ae3070e3f, ComputeTimeInstancesFlood): exit 0 when a client changing its gender three times in a few tics is kicked, and one changing it a second and more apart is not.
//
// usage:  node scenarios/netflood.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] Past the 105-tic window, so the userinfo the client sent on joining no longer counts.
export const SETTLE_TICS = 110;
// [rc4l] Tics between changes: a flood's are inside the 7-tic window, steady ones outside the 42- and 105-tic windows two and three back.
// [rc4l] Gender, since a client holds back its own renames for seconds whatever the server allows.
export const CHANGES = ["gender female", "gender other", "gender male"];
export const GAPS = { flood: 3, steady: 60 };

export function buildNetFloodPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] What is wrong with the server's clients after the changes: the changer kicked or kept as the config wants, the observer always kept.
export function judgeClients(clients, renamer, observer, wantKicked) {
  const has = (p) => (clients || []).some((c) => c.player === p);
  const faults = [];
  if (has(renamer) === wantKicked) faults.push(wantKicked ? `kept player ${renamer}, who flooded` : `kicked player ${renamer}, who changed steadily`);
  if (!has(observer)) faults.push(`dropped the observer, player ${observer}`);
  return faults;
}

export async function judgeNetplay(set, label, { config }) {
  const renamer = set.clients.find((c) => c.role === "player");
  const observer = set.clients.find((c) => c.role === "observer");
  await cap.waitTics(set.server.c, SETTLE_TICS);
  for (const change of CHANGES) {
    await cap.exec(renamer.c, change).catch(() => {});
    await cap.waitTics(set.server.c, GAPS[config.name]);
  }
  await cap.waitTics(set.server.c, 35);
  const clients = (await set.server.c.rpc("net.state", {}))?.clients;
  const faults = judgeClients(clients, renamer.player, observer.player, config.name === "flood");
  // [rc4l] A kicked client sits in its full console, which can leave the set's later checks without an answer, on old builds too.
  if (config.name === "flood") { renamer.c.close(); renamer.c = null; }
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: player ${renamer.player} ${config.name === "flood" ? "kicked" : "kept"}`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "flood", players: 1, observers: 1 }, { name: "steady", players: 1, observers: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netflood", wad: buildNetFloodPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
