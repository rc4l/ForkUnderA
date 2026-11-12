// [rc4l] A client's own ACS global variables across its server's level change (zandronum@70735560bb12): exit 0 when what a client stored is still there on the next map, where the client's new game cleared it.
//
// usage:  node scenarios/netacsvars.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const MAPS = ["MAP01", "MAP02"];
// [rc4l] A clientside script's global, stood in for by the bridge's setacsvar.
export const VAR = { scope: "global", index: 3, value: 42 };

export function buildNetAcsVarsPk3() {
  return pk3(MAPS.map((name) => {
    const m = new UdmfMap();
    m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
    m.thing({ x: 256, y: 128, angle: 90, type: 1 });
    return { name: `maps/${name}.wad`, data: wad(mapLumps(name, m)) };
  }));
}

// [rc4l] What is wrong with the client's map and its getacsvar answer after the change.
export function judgeAcsVars({ map, answer }) {
  const faults = [];
  if (map !== "MAP02") faults.push(`the client is on ${map}, not MAP02`);
  const m = /acsvar (\w+) (\d+) = (-?\d+)/.exec(String(answer));
  if (!m) faults.push(`getacsvar gave no value (${String(answer).trim().slice(0, 120)})`);
  else if (Number(m[3]) !== VAR.value) faults.push(`the client's ${VAR.scope} ${VAR.index} is ${m[3]} on MAP02, not the ${VAR.value} it stored on MAP01`);
  return faults;
}

const mapOf = async (c) => String((await c.rpc("sim.tic", {}))?.map || "").toUpperCase();

export async function judgeNetplay(set, label) {
  const client = set.clients[0].c;
  await cap.exec(client, `setacsvar ${VAR.scope} ${VAR.index} ${VAR.value}`);
  await cap.exec(set.server.c, "changemap MAP02");
  let map = await mapOf(client);
  const until = Date.now() + 20000;
  while (map !== "MAP02" && Date.now() < until) {
    await cap.sleep(250);
    map = await mapOf(client).catch(() => map);
  }
  await cap.waitTics(client, 35);
  const faults = judgeAcsVars({ map, answer: await cap.exec(client, `getacsvar ${VAR.scope} ${VAR.index}`, { quietMs: 300 }) });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the client kept its ${VAR.scope} variable through the level change`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "global", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsvars", wad: buildNetAcsVarsPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
