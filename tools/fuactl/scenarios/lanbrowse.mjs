// [rc4l] A server announcing itself on the LAN: exit 0 when a client's browser lists it as a LAN server under its own name and joining it from there plays the server's map.
//
// usage:  node scenarios/lanbrowse.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import { netdiff, diffLines } from "../src/netdiff.mjs";
import { buildWadDownloadWad } from "./waddownload.mjs";

export const HOSTNAME = "FUA lanbrowse";

// [rc4l] The verdict from the client's join and the row its browser holds for the server.
export function judgeLan(label, { error, row, diffs = [], compared = {} }) {
  const faults = [];
  if (error) faults.push(`the client never got in from its LAN listing: ${error}`);
  else if (!row) faults.push("the browser holds no row for the server");
  else {
    if (!row.lan) faults.push("the server's row did not arrive by LAN broadcast");
    if (row.name !== HOSTNAME) faults.push(`the server is listed as "${row.name}", not "${HOSTNAME}"`);
  }
  if (diffs.length) faults.push(`client: ${diffLines("", diffs, compared).slice(1).join(";").trim()}`);
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the browser listed "${HOSTNAME}" from its LAN announce and the join from there plays the server's map`, failed: 0 };
}

export async function judgeNetplay(set, label) {
  const address = `127.0.0.1:${set.gamePort}`;
  let client;
  try {
    client = await addClient(set, { via: "lan" });
  } catch (e) {
    return judgeLan(label, { error: e.message });
  }
  const row = ((await client.c.rpc("browser.list", {}))?.servers || []).find((s) => s.address === address);
  return judgeLan(label, { row, ...(await netdiff(set.server.c, client.c, { seconds: 1 })) });
}

// [rc4l] One server announcing; --side lan --cvar sv_broadcast=0 shows a silent one failing it.
export const CONFIGS = [{ name: "lan", players: 0, broadcast: true, serverCvars: { sv_hostname: HOSTNAME } }];

runScenarioIfMain(import.meta.url, () => ({ name: "lanbrowse", wad: buildWadDownloadWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
