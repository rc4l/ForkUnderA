// [Doruk-Aksoy] The fua_ ACS shader functions in a hosted game, read on each client with fua_postshaders: exit 0 when a server script's change reaches every client, an activator-only one only the client that puked it, a CLIENTSIDE ENTER script only each entering player's own screen and a CLIENTSIDE puke only its own client, a value set again or a name GLDEFS never declared sends nothing, a timed enable goes off on every client and later when enabled again meanwhile, a late joiner gets everyone's state with its clock but not another player's, a client taking a leaver's slot gets none of the leaver's, a new map starts from GLDEFS, and a round reset clears the shaders and the music volume; on loopback with the server reading GLDEFS as a dedicated one does, and at 100 ms with 5% loss.
//
// usage:  node scenarios/netshaders.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { acsScripts, callFunc, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { scaled } from "../src/timeouts.mjs";
import { GLDEFS, SHADERS, STRINGS, shaderCalls } from "./acsshaders.mjs";
import { silentWav, parseHeard, QUIET, assertQuiet } from "./netmusic.mjs";

const NET_STRINGS = [...STRINGS, "nosuch", "nolocal"];
const { enable, uniform } = shaderCalls(NET_STRINGS);
const SET_MUSIC_VOLUME = 97;
const SONG = "FUASONG1";

// [Doruk-Aksoy] The same calls again each tic for five tics, none changing anything, with three naming what GLDEFS never declared.
const NOTHING_NEW = [...uniform("u3f", "c", [0, 0, 1]), ...enable("u3f", true), ...enable("nosuch", true), ...uniform("u3f", "nosuch", [1]),
  ...uniform("nosuch", "c", [1, 1, 1]), PCD.DELAYDIRECT, 1];

// [Doruk-Aksoy] The scripts, by number: who runs each is in its comment.
export const SCRIPTS = [
  // [Doruk-Aksoy] Puked on the server: a vec3 and a vec4 for everyone.
  { number: 1, code: [...uniform("u3f", "c", [0, 0, 1]), ...enable("u3f", true), ...uniform("u4net", "d", [1, 0, 1, 1]), ...enable("u4net", true)] },
  // [Doruk-Aksoy] Puked by a client (NET): a float and an int only that player's.
  { number: 2, flags: SCRIPTF.NET, code: [...uniform("u1f", "a", [0.5], { own: true }), ...enable("u1f", true, true), ...uniform("u1i", "m", [3], { int: true, own: true })] },
  // [Doruk-Aksoy] Puked on the server: everyone's for a minute, still running when a late joiner arrives.
  { number: 3, code: [...uniform("u2f", "b", [0.25, 0.75]), ...enable("u2f", true, false, 2100)] },
  // [Doruk-Aksoy] Every client runs it for every player who enters; only the entering player's own client applies it.
  { number: 4, type: SCRIPT.ENTER, flags: SCRIPTF.CLIENTSIDE, code: [...uniform("u4f", "d", [0, 1, 0, 1], { own: true }), ...enable("u4f", true, true)] },
  // [Doruk-Aksoy] Puked by a client (CLIENTSIDE): everyone's, on that client alone.
  { number: 5, flags: SCRIPTF.NET | SCRIPTF.CLIENTSIDE, code: [...uniform("u1i", "m", [7], { int: true }), ...enable("u1i", true)] },
  // [Doruk-Aksoy] Puked on the server: on for everyone for three seconds, from when it was last puked.
  { number: 6, code: enable("timed", true, false, 105) },
  // [Doruk-Aksoy] Puked on the server: nothing new five times over, then one real change, everyone's u1f a, which every client gets once.
  { number: 7, code: [...Array(5).fill(NOTHING_NEW).flat(), ...uniform("u1f", "a", [0])] },
  // [Doruk-Aksoy] Puked by a client (CLIENTSIDE): only names GLDEFS never declared, none of them a name the server was asked for, which leave its state as it was.
  { number: 8, flags: SCRIPTF.NET | SCRIPTF.CLIENTSIDE, code: [...enable("nolocal", true), ...uniform("u1i", "nolocal", [1], { int: true }), ...uniform("nolocal", "m", [1], { int: true })] },
  // [Doruk-Aksoy] Puked on the server: the song at half its volume, which a round reset puts back.
  { number: 10, code: [...callFunc(SET_MUSIC_VOLUME, [0.5 * 65536]), PCD.DROP] },
];

export const MAPS = ["MAP01", "MAP02"];
const MAPINFO = MAPS.map((name) => `map ${name} "Shader test"\n{\n\tmusic = "${SONG}"\n}\n`).join("");

// [Doruk-Aksoy] Two copies of one room with four starts and SCRIPTS, the second for the map change, with the shaders and a silent song beside them.
export function buildNetShadersPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [i, y] of [[1, 96], [2, 192], [3, 288], [4, 384]]) m.thing({ x: 96, y, angle: 0, type: i });
  const behavior = acsScripts(SCRIPTS.map((s) => ({ open: false, ...s, code: [...s.code, PCD.TERMINATE] })), NET_STRINGS);
  return pk3([
    ...MAPS.map((name) => ({ name: `maps/${name}.wad`, data: wad(mapLumps(name, m, { behavior })) })),
    { name: "mapinfo.txt", data: MAPINFO },
    { name: `music/${SONG.toLowerCase()}.wav`, data: silentWav() },
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([lump, text]) => ({ name: `shaders/acs/${lump}.fp`, data: text })),
  ]);
}

// [Doruk-Aksoy] What fua_postshaders printed: the state's entries and the tic it was read at, and by shader on or off, since when, and each uniform's four numbers.
export function parseShaderStates(text) {
  const out = { entries: null, at: null, shaders: {} };
  for (const line of String(text).split(/\r?\n/)) {
    const head = /fua_postshaders: (\d+) entries at (-?\d+)/.exec(line);
    if (head) { out.entries = Number(head[1]); out.at = Number(head[2]); continue; }
    const m = /fua_postshaders: (\S+) (on|off)(?: since (-?\d+))?(.*)$/.exec(line);
    if (!m) continue;
    const uniforms = {};
    for (const [, name, values] of m[4].matchAll(/(\w+)=(\S+)/g)) uniforms[name] = values.split(",").map(Number);
    out.shaders[m[1]] = { on: m[2] === "on", since: m[3] === undefined ? null : Number(m[3]), uniforms };
  }
  return out;
}

// [Doruk-Aksoy] How a client's shaders differ from what is wanted ({ on, uniform: [first numbers] }), as faults naming `who`.
export function stateFaults(shaders, want, who) {
  const faults = [];
  for (const [shader, w] of Object.entries(want)) {
    const s = shaders[shader];
    if (!s) { faults.push(`${who} lists no ${shader}`); continue; }
    if (s.on !== w.on) faults.push(`${who} has ${shader} ${s.on ? "on" : "off"}, not ${w.on ? "on" : "off"}`);
    for (const [name, values] of Object.entries(w.uniforms || {})) {
      const got = (s.uniforms[name] || []).slice(0, values.length);
      if (got.length !== values.length || got.some((v, i) => Math.abs(v - values[i]) > 1e-3)) faults.push(`${who} has ${shader}'s ${name} at ${got.join(",")}, not ${values.join(",")}`);
    }
  }
  return faults;
}

// [Doruk-Aksoy] The enable and uniform commands a server's netcap file holds, by extended id (SVC2_FUASETSHADERENABLED 75, SVC2_FUASETSHADERUNIFORM 76).
export function countShaderCommands(text) {
  const counts = { enabled: 0, uniform: 0 };
  for (const line of String(text).split(/\r?\n/)) {
    const f = line.split(" ");
    if (f.length < 5 || f[3] !== "232") continue;
    if (f[4] === "75") counts.enabled++;
    else if (f[4] === "76") counts.uniform++;
  }
  return counts;
}

// [Doruk-Aksoy] Whether a joiner's shader has run as long as an earlier player's, read `gapTics` apart, give or take a second: a joiner's clock restarting from its join is far outside that.
export function clocksAgree(early, late, gapTics) {
  if (early.since === null || late.since === null) return false;
  return Math.abs((late.at - late.since) - (early.at - early.since) - gapTics) <= 35;
}

const OWN_GREEN = { u4f: { on: true, uniforms: { d: [0, 1, 0, 1] } } };
const BLUE = { u3f: { on: true, uniforms: { c: [0, 0, 1] } }, u4net: { on: true, uniforms: { d: [1, 0, 1, 1] } } };
const THEIRS_HALF = { u1f: { on: true, uniforms: { a: [0.5] } }, u1i: { on: false, uniforms: { m: [3] } } };
const NOT_THEIRS = { u1f: { on: false, uniforms: { a: [0] } }, u1i: { on: false, uniforms: { m: [0] } } };
const TIMED_MINUTE = { u2f: { on: true, uniforms: { b: [0.25, 0.75] } } };
const LOCAL_SEVEN = { u1i: { on: true, uniforms: { m: [7] } } };
const FRESH = { u1f: { on: false }, u2f: { on: false }, u3f: { on: false }, u4net: { on: false }, u1i: { on: false }, timed: { on: false } };

const statesOf = async (client) => parseShaderStates(await cap.exec(client.c, "fua_postshaders", { quietMs: 300 }));

// [Doruk-Aksoy] Until a client is connected and playing on `map`.
async function arriveOn(client, map, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const t = (await client.c.rpc("sim.tic").catch(() => ({}))) || {};
    if (t.inlevel && t.map === map && (await client.c.rpc("net.state", {}))?.connection === "active") return true;
    await cap.sleep(150);
  }
  return false;
}

// [Doruk-Aksoy] Until the server no longer holds `player`, as a disconnect that loss drops leaves the slot taken until the server times it out.
async function slotFreed(sc, player, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (!(((await sc.rpc("net.state", {}))?.clients) || []).some((r) => r.player === player)) return true;
    await cap.sleep(250);
  }
  return false;
}

// [Doruk-Aksoy] The commands the server sends while `puke` runs, from a netcap of its own.
async function sentWhile(sc, label, puke, tics) {
  const file = path.join(os.tmpdir(), `fua-netshaders-${label.replace(/\W+/g, "-")}.netcap`).replace(/\\/g, "/");
  fs.rmSync(file, { force: true });
  await cap.exec(sc, `fua_netcap_start "${file}"`);
  await cap.exec(sc, puke);
  await cap.waitTics(sc, tics);
  await cap.exec(sc, "fua_netcap_stop");
  return countShaderCommands(fs.existsSync(file) ? fs.readFileSync(file, "utf8") : "");
}

// [Doruk-Aksoy] A client notices a lost reliable packet only when a later one arrives, so on this quiet map the server says something after each miss, as a level's monsters and sounds would, until the server keeps idle clients told.
const nudge = (sc) => cap.exec(sc, "say .");

// [Doruk-Aksoy] A client's puke rides its unreliable packet, which loss can drop, so it is sent again until the client holds `want`, as a player presses again.
async function pukeFrom(client, sc, script, want, { tries = 5, nudges = false } = {}) {
  for (let i = 0; i < tries; i++) {
    await cap.exec(client.c, `puke ${script}`);
    await cap.waitTics(sc, 35);
    if (!stateFaults((await statesOf(client)).shaders, want, "").length) return;
    if (nudges) await nudge(sc);
  }
}

const verdict = (label, faults, passed) => (faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 } : { line: `PASS ${label}: ${passed}`, failed: 0 });

// [Doruk-Aksoy] Everything but the round reset, in the order a level plays it.
async function judgeFlow(set, label, config) {
  const sc = set.server.c;
  const [p0, p1] = set.clients;
  const faults = [];
  // [Doruk-Aksoy] Under loss a reliable command can arrive seconds late, so a lossy configuration waits a second at a time for what must arrive, `settle` times, and says how late the latest was.
  let slowest = 0;
  const check = async (when, rows) => {
    for (const [client, who, want] of rows) {
      let found = [];
      for (let waited = 0, tries = config.settle ?? 1; tries > 0; tries--, waited++) {
        found = stateFaults((await statesOf(client)).shaders, want, `${when}, ${who}`);
        slowest = Math.max(slowest, waited);
        if (!found.length || tries === 1) break;
        if (config.nudge) await nudge(sc);
        await cap.waitTics(client.c, 35);
      }
      faults.push(...found);
    }
  };
  if (config.reread) {
    const declared = Number((/(\d+) declared/.exec(await cap.exec(sc, "fua_postshaders_reread")) || [])[1]);
    const want = (GLDEFS.match(/HardwareShader PostProcess/g) || []).length;
    if (declared !== want) faults.push(`the server read ${declared} post-process declarations as a dedicated one does, not ${want}`);
  }
  await cap.waitTics(sc, 35);
  await check("on entering", [[p0, "player 0", OWN_GREEN], [p1, "player 1", OWN_GREEN]]);
  await cap.exec(sc, "puke 1");
  await cap.waitTics(sc, 35);
  await check("after the server's change", [[p0, "player 0", BLUE], [p1, "player 1", BLUE]]);
  await pukeFrom(p0, sc, 2, THEIRS_HALF, { nudges: config.nudge });
  await check("after player 0's own change", [[p0, "player 0", THEIRS_HALF], [p1, "player 1", NOT_THEIRS]]);
  await cap.exec(sc, "puke 3");
  await cap.exec(p1.c, "puke 5");
  await cap.waitTics(sc, 35);
  await check("after a timed change and player 1's CLIENTSIDE one", [[p0, "player 0", { ...TIMED_MINUTE, u1i: { on: false, uniforms: { m: [3] } } }], [p1, "player 1", { ...TIMED_MINUTE, ...LOCAL_SEVEN }]]);
  const sent = await sentWhile(sc, label, "puke 7", 35);
  if (sent.enabled !== 0 || sent.uniform !== set.clients.length) {
    faults.push(`for values set again, undeclared names and one change the server sent ${sent.enabled} enables and ${sent.uniform} uniforms, not 0 and ${set.clients.length}`);
  }
  const before = (await statesOf(p1)).entries;
  await cap.exec(p1.c, "puke 8");
  await cap.waitTics(sc, 20);
  const after = (await statesOf(p1)).entries;
  if (before === null || after !== before) faults.push(`player 1's CLIENTSIDE calls naming undeclared shaders took its state from ${before} entries to ${after}`);
  // [Doruk-Aksoy] Timing to the tic, which loss's resends blur, is judged where nothing is lost.
  const timed = config.timing !== false;
  if (timed) {
    // [Doruk-Aksoy] Three seconds puked at 0 and again at 70 tics: still on at 140, where the first would have ended, and off by 230, a resend's time past its end.
    await cap.exec(sc, "puke 6");
    await cap.waitTics(sc, 35);
    await check("as a three-second change runs", [[p0, "player 0", { timed: { on: true } }], [p1, "player 1", { timed: { on: true } }]]);
    await cap.waitTics(sc, 35);
    await cap.exec(sc, "puke 6");
    await cap.waitTics(sc, 70);
    await check("past its first end, enabled again meanwhile", [[p0, "player 0", { timed: { on: true } }], [p1, "player 1", { timed: { on: true } }]]);
    await cap.waitTics(sc, 90);
    await check("once it ran out", [[p0, "player 0", { timed: { on: false } }], [p1, "player 1", { timed: { on: false } }]]);
  }
  const late = await addClient(set, { role: "latejoin", lag: Number(config.lag || 0), loss: Number(config.loss || 0), cvars: config.clientCvars });
  await cap.waitTics(late.c, 35);
  await check("on joining late", [[late, `late joiner (player ${late.player})`, { ...BLUE, ...TIMED_MINUTE, ...NOT_THEIRS, ...OWN_GREEN, timed: { on: false } }]]);
  if (timed) {
    const clockOf = async (client) => {
      const s = await statesOf(client);
      return { at: s.at, since: s.shaders.u2f?.since ?? null, read: Date.now() };
    };
    const early = await clockOf(p0), lateClock = await clockOf(late);
    const gapTics = Math.round(((lateClock.read - early.read) / 1000) * 35);
    if (!clocksAgree(early, lateClock, gapTics)) {
      faults.push(`the late joiner's u2f has run ${lateClock.at - lateClock.since} tics where player 0's had run ${early.at - early.since}, ${gapTics} tics earlier`);
    }
  }
  // [Doruk-Aksoy] Player 0 leaves and a new client takes the slot, which must not get the leaver's own u1f.
  await cap.exec(p0.c, "disconnect");
  if (!(await slotFreed(sc, p0.player, scaled(60000)))) faults.push(`the server still held player ${p0.player} a minute after they left`);
  const heir = await addClient(set, { role: "player", lag: Number(config.lag || 0), loss: Number(config.loss || 0), cvars: config.clientCvars });
  await cap.waitTics(heir.c, 35);
  if (heir.player !== p0.player) faults.push(`the client joining after player ${p0.player} left took slot ${heir.player}, not theirs`);
  await check("on taking a leaver's slot", [[heir, `player ${heir.player} (new)`, { ...BLUE, ...NOT_THEIRS, ...OWN_GREEN }]]);
  // [Doruk-Aksoy] A new map, every connected client sent there to reconnect; left to loopback, as reconnecting at once under loss is the session's to prove, not the shaders'.
  if (config.mapChange !== false) {
    const stayed = set.clients.filter((c) => c !== p0);
    await cap.exec(sc, `map ${MAPS[1]}`, cap.SLOW_COMMAND);
    for (const c of stayed) if (!(await arriveOn(c, MAPS[1], scaled(60000)))) faults.push(`player ${c.player} never arrived on ${MAPS[1]}`);
    await cap.waitInLevel(sc, scaled(30000));
    await cap.waitTics(sc, 70);
    await check(`on ${MAPS[1]}`, stayed.map((c) => [c, `player ${c.player}`, { ...FRESH, ...OWN_GREEN }]));
  }
  return verdict(label, faults, `everyone's changes reached every client, own ones only their player's, CLIENTSIDE ones only their client; nothing was sent for a value set again or an undeclared name; ${config.timing !== false ? "a timed one ran out everywhere and later when enabled again; a late joiner got everyone's with its clock" : "a late joiner got everyone's"} and a slot's new owner nobody else's${config.mapChange !== false ? `, and ${MAPS[1]} started from GLDEFS` : ""}${(config.settle ?? 1) > 1 ? `; the slowest change arrived about ${slowest} s late` : ""}`);
}

// [Doruk-Aksoy] A song the client plays and its volume, as netmusic reads them.
const heard = async (c) => parseHeard(await cap.exec(c.c, "changemus"), await cap.exec(c.c, "testmusicvol"));

// [Doruk-Aksoy] A survival round with shaders on and the song at half, reset from the server: every client and a joiner after it start clean.
async function judgeReset(set, label, config) {
  const sc = set.server.c;
  const [p0, p1] = set.clients;
  const faults = [];
  const check = async (when, rows) => {
    for (const [client, who, want, song] of rows) {
      faults.push(...stateFaults((await statesOf(client)).shaders, want, `${when}, ${who}`));
      const got = await heard(client);
      if (got !== song) faults.push(`${when}, ${who} hears ${got}, not ${song}`);
    }
  };
  for (const c of set.clients) await assertQuiet(c.c, `player ${c.player}`);
  await cap.waitTics(sc, 35);
  await cap.exec(sc, "puke 1");
  await pukeFrom(p0, sc, 2, THEIRS_HALF);
  await cap.exec(sc, "puke 10");
  await cap.waitTics(sc, 35);
  await check("before the reset", [[p0, "player 0", { ...BLUE, ...THEIRS_HALF }, `${SONG} @ 0.50`], [p1, "player 1", BLUE, `${SONG} @ 0.50`]]);
  await cap.exec(sc, "resetmap");
  await cap.waitTics(sc, 70);
  await check("after the reset", [[p0, "player 0", FRESH, `${SONG} @ 1.00`], [p1, "player 1", FRESH, `${SONG} @ 1.00`]]);
  const late = await addClient(set, { role: "latejoin", cvars: config.clientCvars });
  await assertQuiet(late.c, "the late joiner");
  await cap.waitTics(late.c, 35);
  await check("joining after the reset", [[late, `late joiner (player ${late.player})`, FRESH, `${SONG} @ 1.00`]]);
  return verdict(label, faults, "a round reset turned every script's shaders off and the song back to its own volume on every client, and a joiner after it got none of them");
}

export async function judgeNetplay(set, label, { config }) {
  return config.run === "reset" ? judgeReset(set, label, config) : judgeFlow(set, label, config);
}

// [Doruk-Aksoy] --side loopback --cvar fua_debug_netdrop=232/75 leaves every enable unsent, which fails; the reset's clients play the song, quietly.
export const CONFIGS = [
  { name: "loopback", players: 2, reread: true },
  { name: "lossy", players: 2, lag: "100", loss: "5", settle: 20, timing: false, mapChange: false, nudge: true },
  { name: "roundreset", players: 2, run: "reset", serverCvars: { survival: 1, sv_survivalcountdowntime: 600 }, clientCvars: QUIET },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netshaders", wad: buildNetShadersPk3(), maps: [MAPS[0]], configs: CONFIGS, judgeNetplay }), compareNetplay);
