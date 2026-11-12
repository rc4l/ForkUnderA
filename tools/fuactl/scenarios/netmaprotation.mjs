// [rc4l] ACS's SetMapUsedStatus on a server (zandronum@f522876fe386): exit 0 when the script marks the rotation's third map used and the client's maplist shows it, where neither knew the function.
//
// usage:  node scenarios/netmaprotation.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const MAPS = ["MAP01", "MAP02", "MAP03"];
// [rc4l] The rotation entry the script marks, as ACS counts them from 1.
export const ENTRY = 3;
export const STRINGS = ["set "];

// [rc4l] Script 1: SetMapUsedStatus(3, true), its result logged.
export function buildScript() {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, 0, PCD.PRINTSTRING, PCD.PUSHNUMBER, ENTRY, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.SetMapUsedStatus,
    PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE];
}

export function buildNetMapRotationPk3() {
  const lumps = MAPS.map((name) => {
    const m = new UdmfMap();
    m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
    for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
    const behavior = name === "MAP01" ? acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED }], STRINGS) : undefined;
    return { name: `maps/${name}.wad`, data: wad(mapLumps(name, m, behavior ? { behavior } : {})) };
  });
  return pk3(lumps);
}

// [rc4l] Whether a maplist shows the map as used.
export function showsUsed(text, map) {
  return new RegExp(`\\(Used\\) ${map}\\b`).test(String(text));
}

// [rc4l] What is wrong with the server's and the client's maplists before and after the script.
export function judgeMapLists({ result, serverBefore, clientBefore, serverAfter, clientAfter }) {
  const map = MAPS[ENTRY - 1];
  const faults = [];
  if (!new RegExp(`\\b${map}\\b`).test(String(clientBefore))) faults.push(`the client's rotation has no ${map}`);
  if (showsUsed(serverBefore, map) || showsUsed(clientBefore, map)) faults.push(`${map} was used before the script`);
  if (result !== 1) faults.push(`SetMapUsedStatus returned ${result}, not 1`);
  if (!showsUsed(serverAfter, map)) faults.push(`the server's maplist does not show ${map} used`);
  if (!showsUsed(clientAfter, map)) faults.push(`the client's maplist does not show ${map} used`);
  return faults;
}

export async function judgeNetplay(set, label) {
  const server = set.server.c, client = set.clients[0].c;
  for (const map of MAPS) await cap.exec(server, `addmap ${map}`);
  await cap.waitTics(server, 35);
  const serverBefore = await cap.exec(server, "maplist");
  const clientBefore = await cap.exec(client, "maplist");
  const result = Number(/set (-?\d+)/.exec(String(await cap.exec(server, "puke 1", { quietMs: 300 })))?.[1]);
  await cap.waitTics(server, 35);
  const faults = judgeMapLists({ result, serverBefore, clientBefore, serverAfter: await cap.exec(server, "maplist"), clientAfter: await cap.exec(client, "maplist") });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${MAPS[ENTRY - 1]} shows used on the server and the client`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "used", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netmaprotation", wad: buildNetMapRotationPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
