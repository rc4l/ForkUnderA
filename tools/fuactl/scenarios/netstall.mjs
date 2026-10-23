// [rc4l] A hosted map whose server tick stalls 300 ms once a second, the tick right after it pings, with a client at 100 ms of ping: exit 0 when the server still measures that client's ping as about 100 ms (from when the reply arrived, not when the stalled tick read it) and the client stays in the game.
//
// usage:  node scenarios/netstall.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { buildWadDownloadWad } from "./waddownload.mjs";
import * as cap from "../src/capture.mjs";

export const STALL_MS = 300, LAG_MS = 100;
// [rc4l] A loopback round trip on top of the fake ping, and the tic a reply can wait to be read by a tick that is not stalled.
export const SLACK_MS = 40;

// [rc4l] The verdict from the pings the server measured for the client over the run, and whether the client was still playing.
export function judgeStall(label, { pings, playing }) {
  const faults = [];
  if (!pings.length) faults.push("the server measured no ping for the client");
  const worst = pings.length ? Math.max(...pings) : 0;
  if (worst > LAG_MS + SLACK_MS) faults.push(`the server measured up to ${worst} ms for a client ${LAG_MS} ms away, its ${STALL_MS} ms stall counted as the client's lag`);
  if (!playing) faults.push("the client left the game during the stalls");
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: ${pings.length} pings, the worst ${worst} ms for a client ${LAG_MS} ms away through ${STALL_MS} ms stalls`, failed: 0 };
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [client] = set.clients;
  await cap.waitTics(sc, 35 * 3);
  const pings = [];
  let row = null;
  const until = Date.now() + 8000;
  while (Date.now() < until) {
    row = ((await sc.rpc("net.state", {}))?.clients || []).find((c) => c.player === client.player);
    if (row && row.ping > 0) pings.push(row.ping);
    await cap.sleep(200);
  }
  return judgeStall(label, { pings, playing: !!row && row.state === "spawned" });
}

// [rc4l] One client at 100 ms through a stall each second; --side stall300 --cvar sv_fua_receivethread=0 shows the stall read as ping.
export const CONFIGS = [{ name: "stall300", players: 1, lag: String(LAG_MS), serverCvars: { fua_debug_server_stall_ms: STALL_MS } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netstall", wad: buildWadDownloadWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
