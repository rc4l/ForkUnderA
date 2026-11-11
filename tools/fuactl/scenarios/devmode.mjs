// [rc4l] A hosted map with and without sv_cheats, a client holding its own fua_mesh_sky 0: exit 0 when a client of the server without cheats reads the default, is refused a dev-mode setting and command and gets its own value back once the server allows cheats, while the server's console and a server with cheats allow both until cheats go off (features/dev-mode).
//
// usage:  node scenarios/devmode.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { buildWadDownloadWad } from "./waddownload.mjs";
import * as cap from "../src/capture.mjs";
import { cvarValueOf } from "../src/abperf.mjs";

export const SETTING = "fua_mesh_sky", COMMAND = "fua_mesh_blank";
export const REFUSED = /is a dev-mode tool: sv_cheats must be true on this server/;
const RAN = /mesh pieces blanked/;

// [rc4l] The verdict from what the client and server printed: open allows the client everything until the server turns cheats off; closed holds its value at the default, refuses it both tools and gives the value back once the server turns cheats on.
export function judgeDevMode(label, { open, joined, set, run, server, flipped }) {
  const faults = [];
  const want = (got, value, what) => { if (got !== value) faults.push(`${what} read ${got ?? "nothing"}, not ${value}`); };
  want(joined, open ? "false" : "true", `in the game, the client's own ${SETTING} 0`);
  if (REFUSED.test(set.text) === open) faults.push(open ? `the client was refused ${SETTING} with cheats on` : `the client was not told ${SETTING} is a dev-mode tool`);
  want(set.value, "true", `after the client's ${SETTING} ${open ? 1 : 0}, ${SETTING}`);
  if (open ? REFUSED.test(run) || !RAN.test(run) : !REFUSED.test(run) || RAN.test(run)) faults.push(open ? `${COMMAND} did not run with cheats on` : `${COMMAND} ran on a client without cheats`);
  want(server, "false", `on the server's own console, ${SETTING} 0`);
  want(flipped, open ? "true" : "false", `once the server turned cheats ${open ? "off" : "on"}, the client's own ${SETTING} 0`);
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: ${open ? "the client used both dev-mode tools" : "the client was refused both dev-mode tools and kept its own value"}`, failed: 0 };
}

// [rc4l] Until the client holds the server's flipped sv_cheats, which the server sends it with each map's start (or the time runs out).
async function untilClientCheats(c, value, timeoutMs = 20000) {
  const until = Date.now() + timeoutMs;
  while (Date.now() < until && cvarValueOf(await cap.exec(c, "sv_cheats"), "sv_cheats") !== value) await cap.sleep(250);
  await cap.waitTics(c, 10);
}

export async function judgeNetplay(set, label, { config, map }) {
  const sc = set.server.c, c = set.clients[0].c;
  const read = async (conn, name = SETTING) => cvarValueOf(await cap.exec(conn, name), name);
  const open = config.cheats !== false;
  const joined = await read(c);
  const setText = await cap.exec(c, `${SETTING} ${open ? 1 : 0}`);
  const attempt = { text: setText, value: await read(c) };
  await cap.exec(c, `${SETTING} 0`);
  // [rc4l] The client prints the refusal at most once a second.
  await cap.waitTics(c, 40);
  const run = await cap.exec(c, COMMAND);
  await cap.exec(sc, `${SETTING} 0`);
  const server = await read(sc);
  await cap.exec(sc, `${SETTING} 1`);
  // [rc4l] sv_cheats is latched, so the flip takes effect with the map started again.
  await cap.exec(sc, `sv_cheats ${open ? 0 : 1}; map ${map}`, { quietMs: 600 });
  await untilClientCheats(c, open ? "false" : "true");
  const flipped = await read(c);
  return judgeDevMode(label, { open, joined, set: attempt, run, server, flipped });
}

// [rc4l] The client brings its own value and sv_cheats 1 from offline, which a server without cheats overrides.
export const CONFIGS = [
  { name: "nocheats", players: 1, cheats: false, clientCvars: { [SETTING]: 0, sv_cheats: 1 } },
  { name: "cheats", players: 1, clientCvars: { [SETTING]: 0 } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "devmode", wad: buildWadDownloadWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
