// [rc4l] A hosted map with and without sv_cheats, a client holding its own value of each dev-mode setting in TOOLS: exit 0 when a client of the server without cheats reads the defaults, is refused every dev-mode setting and command and gets its own values back once the server allows cheats, while the server's console and a server with cheats allow them all until cheats go off (features/dev-mode).
//
// usage:  node scenarios/devmode.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { buildWadDownloadWad } from "./waddownload.mjs";
import * as cap from "../src/capture.mjs";
import { cvarValueOf } from "../src/abperf.mjs";

// [rc4l] Each dev-mode setting with the client's own value (its default is the other), and a dev-mode command with what it prints when it runs.
export const TOOLS = [
  { setting: "fua_mesh_sky", own: 0, command: "fua_mesh_blank", ran: /mesh pieces blanked/ },
  { setting: "cl_fua_hitbox", own: 1, command: "fua_hitbox", ran: /Hitbox overlay (on|off)\./ },
  { setting: "fua_dg_lightlinks", own: 0, command: "fua_light", ran: /usage: fua_light/ },
];
export const REFUSED = /is a dev-mode tool: sv_cheats must be true on this server/;
const shown = (v) => (v ? "true" : "false");

// [rc4l] One tool's faults from what the client and server printed: open allows the client everything until the server turns cheats off; closed holds its value at the default, refuses it both and gives the value back once the server turns cheats on.
export function toolFaults({ setting, own, command, ran }, open, { joined, set, run, server, flipped }) {
  const faults = [];
  const want = (got, value, what) => { if (got !== value) faults.push(`${what} read ${got ?? "nothing"}, not ${value}`); };
  const mine = shown(own), fallback = shown(1 - own);
  want(joined, open ? mine : fallback, `in the game, the client's own ${setting} ${own}`);
  if (REFUSED.test(set.text) === open) faults.push(open ? `the client was refused ${setting} with cheats on` : `the client was not told ${setting} is a dev-mode tool`);
  want(set.value, fallback, `after the client's ${setting} ${open ? 1 - own : own}, ${setting}`);
  if (open ? REFUSED.test(run) || !ran.test(run) : !REFUSED.test(run) || ran.test(run)) faults.push(open ? `${command} did not run with cheats on` : `${command} ran on a client without cheats`);
  want(server, mine, `on the server's own console, ${setting} ${own}`);
  want(flipped, open ? fallback : mine, `once the server turned cheats ${open ? "off" : "on"}, the client's own ${setting} ${own}`);
  return faults;
}

// [rc4l] The verdict over every tool, readings in TOOLS order.
export function judgeDevMode(label, open, readings) {
  const faults = TOOLS.flatMap((tool, i) => toolFaults(tool, open, readings[i]));
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: ${open ? "the client used every dev-mode tool" : "the client was refused every dev-mode tool and kept its own values"}`, failed: 0 };
}

// [rc4l] Until the client holds the server's flipped sv_cheats, which the server sends it with each map's start (or the time runs out).
async function untilClientCheats(c, value, timeoutMs = 20000) {
  const until = Date.now() + timeoutMs;
  while (Date.now() < until && cvarValueOf(await cap.exec(c, "sv_cheats"), "sv_cheats") !== value) await cap.sleep(250);
  await cap.waitTics(c, 10);
}

export async function judgeNetplay(set, label, { config, map }) {
  const sc = set.server.c, c = set.clients[0].c;
  const read = async (conn, name) => cvarValueOf(await cap.exec(conn, name), name);
  const open = config.cheats !== false;
  const readings = [];
  for (const { setting, own, command } of TOOLS) {
    const joined = await read(c, setting);
    // [rc4l] The client prints the refusal at most once a second.
    await cap.waitTics(c, 40);
    const text = await cap.exec(c, `${setting} ${open ? 1 - own : own}`);
    const attempt = { text, value: await read(c, setting) };
    await cap.waitTics(c, 40);
    const run = await cap.exec(c, command);
    await cap.exec(c, `${setting} ${own}`);
    await cap.exec(sc, `${setting} ${own}`);
    const server = await read(sc, setting);
    await cap.exec(sc, `${setting} ${1 - own}`);
    readings.push({ joined, set: attempt, run, server });
  }
  // [rc4l] sv_cheats is latched, so the flip takes effect with the map started again.
  await cap.exec(sc, `sv_cheats ${open ? 0 : 1}; map ${map}`, { quietMs: 600 });
  await untilClientCheats(c, open ? "false" : "true");
  for (let i = 0; i < TOOLS.length; i++) readings[i].flipped = await read(c, TOOLS[i].setting);
  return judgeDevMode(label, open, readings);
}

// [rc4l] The client brings its own values and sv_cheats 1 from offline, which a server without cheats overrides.
const own = Object.fromEntries(TOOLS.map((t) => [t.setting, t.own]));
export const CONFIGS = [
  { name: "nocheats", players: 1, cheats: false, clientCvars: { ...own, sv_cheats: 1 } },
  { name: "cheats", players: 1, clientCvars: own },
];

runScenarioIfMain(import.meta.url, () => ({ name: "devmode", wad: buildWadDownloadWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
