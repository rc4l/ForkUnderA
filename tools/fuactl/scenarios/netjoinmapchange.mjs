// [Doruk-Aksoy] A map change an empty server holds until somebody connects, as DnD's setsoftcore does: the joiner, asked for MAP01 and answering after the server moved to MAP02, must be asked again and come in, not be refused for level authentication.
//
// usage:  node scenarios/netjoinmapchange.mjs
import fs from "node:fs";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, consoleCommand, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient, clientPlaying, connectionTrouble } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

// [Doruk-Aksoy] The closed script the server pukes, which runs only once a client's connection wakes the empty server's playsim.
export const SCRIPT = 1;

// [Doruk-Aksoy] MAP01 holds the script that sends the server to MAP02, and each map is lit its own way so a client on the wrong one shows.
export function buildNetJoinMapChangeWad() {
  return wad(["MAP01", "MAP02"].flatMap((name, i) => {
    const m = new UdmfMap();
    m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 + 32 * i },
      () => ({ side: { texturemiddle: "STARTAN2" } }));
    m.thing({ x: 256, y: 256, angle: 90, type: 1 });
    const behavior = i === 0 ? acs0([...consoleCommand(0), PCD.TERMINATE], ["map MAP02"], { open: false, number: SCRIPT }) : undefined;
    return mapLumps(name, m, behavior ? { behavior } : {});
  }));
}

// [Doruk-Aksoy] The faults from what was seen: the server held the change until the joiner came, then both ended on MAP02 with the joiner playing and never refused.
export function judgeJoin({ heldMap, serverMap, clientMap, playing, trouble }) {
  const faults = [];
  if (heldMap !== "MAP01") faults.push(`the empty server ran the change before anybody joined (on ${heldMap || "no map"}), so the race was never set up`);
  if (trouble) faults.push(`the joiner was refused: "${trouble}"`);
  if (serverMap !== "MAP02") faults.push(`the server is on ${serverMap || "no map"}, not MAP02`);
  if (clientMap !== "MAP02") faults.push(`the joiner is on ${clientMap || "no map"}, not MAP02`);
  if (!playing) faults.push("the server does not hold the joiner playing");
  return faults;
}

const mapOf = async (c) => ((await c.rpc("sim.tic").catch(() => ({}))) || {}).map;

export async function judgeNetplay(set, label, { config }) {
  const sc = set.server.c;
  await cap.exec(sc, `puke ${SCRIPT}`, { quietMs: 300 });
  await cap.waitTics(sc, 35).catch(() => {});
  const heldMap = await mapOf(sc);
  let client = null;
  let trouble = null;
  try {
    client = await addClient(set, { via: "browser", lag: config.lag ? Number(config.lag) : 0 });
  } catch (e) {
    trouble = e.message;
  }
  const joiner = set.clients[set.clients.length - 1];
  const log = joiner?.inst?.log && fs.existsSync(joiner.inst.log) ? fs.readFileSync(joiner.inst.log, "utf8") : "";
  // [Doruk-Aksoy] Fetching the template wad on a first run is the browser's way in, not a refusal.
  trouble = log.split(/\r?\n/).map((l) => l.trim()).find((l) => connectionTrouble(l) && !/^Downloading/.test(l)) || trouble;
  await cap.waitInLevel(sc).catch(() => {});
  const serverMap = await mapOf(sc);
  const clientMap = joiner?.c ? await mapOf(joiner.c) : undefined;
  const playing = !!client && clientPlaying(await sc.rpc("net.state", {}), client.player);
  const faults = judgeJoin({ heldMap, serverMap, clientMap, playing, trouble });
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the empty server held its change to MAP02 until the joiner connected, and the joiner plays on MAP02`, failed: 0 };
}

// [Doruk-Aksoy] The server alone, the joiner added by the judge; at 100 ms the joiner's answer lands after the change every time.
export const CONFIGS = [
  { name: "lan", players: 0, observers: 0 },
  { name: "ping100", players: 0, observers: 0, lag: "100" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netjoinmapchange", wad: buildNetJoinMapChangeWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
