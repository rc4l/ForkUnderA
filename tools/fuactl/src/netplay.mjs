// [rc4l] A netgame for tests on loopback: a locked server hosting a map and clients that join it as players or as observers (players another player is judged by), each with its own simulated ping and loss, torn down together.
import fs from "node:fs";
import { launchInstance, stopInstance } from "./launch.mjs";
import { attach } from "./session.mjs";
import * as cap from "./capture.mjs";
import { scaled } from "./timeouts.mjs";
import { parseSeamReport } from "./seams.mjs";

export const NETPLAY_BRIDGE_PORT = 7820;
export const NETPLAY_GAME_PORT = 10820;

// [rc4l] A server's arguments: its wads, -host on the game port (a +port cvar is ignored), no announcing to the registry and none on the LAN unless broadcast (then left to its cvar), cheats from launch (a later change never reaches a joined client), any number of clients from one address (every client of a set is 127.0.0.1), and loopback only.
export function serverArgs({ files = [], gamePort = NETPLAY_GAME_PORT, broadcast = false } = {}) {
  return [...(files.length ? ["-file", ...files] : []), "-host", "-port", String(gamePort), ...(broadcast ? [] : ["+sv_broadcast", "0"]), "+sv_updatemaster", "0",
    "+sv_cheats", "1", "+sv_maxclientsperip", "0", "-useip", "127.0.0.1"];
}

// [rc4l] A client's arguments: its wads, loopback only, and the WebGPU backend when asked.
export function clientArgs({ files = [], webgpu = false } = {}) {
  return [...(files.length ? ["-file", ...files] : []), "-useip", "127.0.0.1", ...(webgpu ? ["-webgpu"] : [])];
}

// [rc4l] A client's cvars: presenting without the focus, no public server registry to ask, its fake ping and loss when set, then the caller's.
export function clientCvars({ lag = 0, loss = 0, cvars = {} } = {}) {
  return { fua_render_in_background: 1, cl_fua_serverregistry_list: "", cl_fua_serverregistrylist_fetch: 0, ...(lag > 0 ? { fua_net_fakeping: lag } : {}), ...(loss > 0 ? { fua_net_fakeloss: loss } : {}), ...cvars };
}

// [rc4l] One value per client from a comma list, "100,0" for the first two, a lone value for every client, the last given for any past the list.
export function perClient(spec, count) {
  const values = String(spec ?? "").split(",").map((s) => s.trim()).filter((s) => s !== "").map(Number);
  if (values.some((v) => !Number.isFinite(v) || v < 0)) throw new Error(`expected comma-separated numbers of 0 or more, not "${spec}"`);
  return Array.from({ length: count }, (_, i) => (values.length ? values[Math.min(i, values.length - 1)] : 0));
}

// [rc4l] The clients a set launches in order: players first, then observers, the late joiner last when asked.
export function clientRoles({ players = 1, observers = 0, lateJoin = false } = {}) {
  const roles = [...Array(players).fill("player"), ...Array(observers).fill("observer")];
  if (lateJoin) roles.push("latejoin");
  return roles;
}

// [rc4l] Whether the server holds the client playing (spawned and not spectating), from its net.state.
export function clientPlaying(serverState, player) {
  const row = (serverState?.clients || []).find((c) => c.player === player);
  return !!row && row.state === "spawned" && !row.spectating;
}

// [rc4l] Why a client's log says it never got in, from the refusals cl_main.cpp prints and its own quit, or null.
const REFUSALS = /Too many connections from your IP|Server is full|Incorrect password|Failed connect\.|Couldn't connect\.|authentication failed|could not confirm who you are|already playing on your account|rejected the userinfo|Unknown error code|Unable to recover|Can't join|Won't download|No usable download sites|Downloading \d+ files? to/i;
export function connectionTrouble(logText) {
  return String(logText ?? "").split(/\r?\n/).find((l) => REFUSALS.test(l))?.trim() || null;
}

// [rc4l] An error about a client, with the reason its log gives when it has one.
export function withTrouble(error, logText) {
  const why = connectionTrouble(logText);
  return why ? new Error(`${error.message}: the client says "${why}"`) : error;
}

// [rc4l] A join as a player makes one from the browser: the server added by address and queried (or, with lan, heard announcing itself), picked once it answers, then fua_join_selected_server, which fetches what the client lacks and reloads onto the server's wads.
export async function joinThroughBrowser(c, address, deps = {}, { lan = false } = {}) {
  const { exec = cap.exec, sleep = cap.sleep, timeoutMs = scaled(60000) } = deps;
  // [rc4l] The browser on screen, as a player has it, so a finished download joins at once rather than waiting to be come back to.
  await exec(c, "openmenu ZA_Browser");
  if (!lan) await c.rpc("browser.add", { address });
  const deadline = Date.now() + timeoutMs;
  while (!((await c.rpc("browser.list", {}))?.servers || []).some((s) => s.address === address && s.name && (!lan || s.lan))) {
    if (Date.now() > deadline) throw new Error(lan ? `the browser never heard ${address} announce itself on the LAN` : `the browser never heard back from ${address}`);
    await sleep(250);
  }
  await c.rpc("browser.select", { address });
  await exec(c, "fua_join_selected_server");
  // [rc4l] Connected, since the title's demo is a level too and a download comes first.
  while ((await c.rpc("net.state", {}).catch(() => ({})))?.connection !== "active") {
    if (Date.now() > deadline) throw new Error(`never connected to ${address} after joining from the browser`);
    await sleep(250);
  }
}

// [rc4l] Launches one client into a set, joins it to the game and waits until the server holds it playing; via "browser" joins from the browser by address and "lan" from its LAN listing, and files replaces the set's wads, empty for a client that has to fetch them.
export async function addClient(set, { role = "player", lag = 0, loss = 0, cvars = {}, forUser = false, files = set.files, via = "connect" } = {}, deps = {}) {
  const { launch = launchInstance, connect = attach, waitInLevel = cap.waitInLevel, exec = cap.exec, sleep = cap.sleep, timeoutMs = scaled(60000) } = deps;
  const index = set.clients.length;
  // [rc4l] The slot is taken before the launch, so clients added at once each get their own port.
  const client = { inst: null, c: null, role, lag, loss, player: -1 };
  set.clients.push(client);
  const address = `127.0.0.1:${set.gamePort}`;
  const fromBrowser = via === "browser" || via === "lan";
  const inst = client.inst = await launch({ port: set.basePort + 1 + index, iwad: set.iwad, extraArgs: clientArgs({ ...set, files }), ...(fromBrowser ? {} : { connect: address }),
    cvars: clientCvars({ lag, loss, cvars }), allowOsInput: forUser });
  const readLog = deps.readLog || ((i) => (i.log && fs.existsSync(i.log) ? fs.readFileSync(i.log, "utf8") : ""));
  try {
    client.c = await connect(inst);
    if (fromBrowser) await joinThroughBrowser(client.c, address, { exec, sleep, timeoutMs }, { lan: via === "lan" });
    await waitInLevel(client.c, timeoutMs);
  } catch (e) {
    throw withTrouble(e, readLog(inst));
  }
  // [rc4l] A client arrives a spectator, which the server sends only keepalives.
  await exec(client.c, "join");
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const mine = await client.c.rpc("net.state", {});
    client.player = mine?.consoleplayer ?? -1;
    if (mine?.connection === "active" && clientPlaying(await set.server.c.rpc("net.state", {}), client.player)) return client;
    if (Date.now() > deadline) throw withTrouble(new Error(`client ${index} (${role}) never joined the game: ${JSON.stringify(mine)}`), readLog(inst));
    await sleep(250);
  }
}

// [rc4l] Each instance of a set whose seams a thread their owner does not allow entered, as "who: first (count)", since the netcode's seams run only in a netgame.
export async function seamViolations(set, exec = cap.exec) {
  const out = [];
  for (const m of [set.server, ...set.clients]) {
    if (!m?.c) continue;
    const r = parseSeamReport(await exec(m.c, "fua_seams"));
    if (r.differed > 0) out.push(`${m.role}${m.player >= 0 ? ` player ${m.player}` : ""}: ${r.differed} seam violations, the first in ${r.first}`);
  }
  return out;
}

// [rc4l] What fua_net_replication says a server sent late, a change no Touch marked, or "" when nothing was.
export function replicationMisses(text) {
  const m = /replication: (\d+) rows sent, (\d+) missed(?:, the first (.+))?/.exec(String(text ?? ""));
  return m && Number(m[2]) > 0 ? `the server sent ${m[2]} changed rows a tic late, untouched, the first ${m[3]}` : "";
}

// [rc4l] Every connection closed and every instance stopped, clients first.
export async function closeNetplay(set, stop = stopInstance) {
  const all = [...set.clients].reverse().concat(set.server ? [set.server] : []);
  for (const m of all) if (m.c) m.c.close();
  for (const m of all) if (m.inst) await stop(m.inst);
}

// [rc4l] The set: a server in the map, then each client of clientRoles joined in turn (the late joiner left to the caller's addClient).
export async function launchNetplay(opts = {}, deps = {}) {
  const { launch = launchInstance, connect = attach, waitInLevel = cap.waitInLevel, stop = stopInstance, timeoutMs = scaled(60000) } = deps;
  const { iwad, map, files = [], players = 1, observers = 0, lag = "", loss = "", basePort = NETPLAY_BRIDGE_PORT, gamePort = NETPLAY_GAME_PORT,
    forUser = false, cvars = {}, clientCvars: perClientCvars = {}, broadcast = false } = opts;
  const roles = clientRoles({ players, observers });
  const lags = perClient(lag, roles.length), losses = perClient(loss, roles.length);
  const set = { iwad, map, files, basePort, gamePort, server: null, clients: [] };
  try {
    // [rc4l] Announcing through its cvar, so a caller's sv_broadcast 0 still turns it off.
    const inst = await launch({ port: basePort, iwad, map, extraArgs: serverArgs({ files, gamePort, broadcast }), cvars: { ...(broadcast ? { sv_broadcast: 1 } : {}), ...cvars } });
    set.server = { inst, c: null, role: "server" };
    set.server.c = await connect(inst);
    await waitInLevel(set.server.c, timeoutMs);
    for (let i = 0; i < roles.length; i++) {
      await addClient(set, { role: roles[i], lag: lags[i], loss: losses[i], cvars: perClientCvars, forUser: forUser && i === 0 }, deps);
    }
    return set;
  } catch (e) {
    await closeNetplay(set, stop);
    throw e;
  }
}

// [rc4l] The set as the session file records it, so later commands reach each instance by port and token.
export function netplaySession(set) {
  const row = (m) => ({ role: m.role, port: m.inst.port, token: m.inst.token, pid: m.inst.pid, player: m.player ?? -1, lag: m.lag ?? 0, loss: m.loss ?? 0 });
  return { gamePort: set.gamePort, map: set.map, server: row(set.server), clients: set.clients.map(row) };
}

// [rc4l] The netplay options a command line asks for: --map, --iwad, --file a,b, --players, --observers, --lag and --loss per client, --port (the server's bridge, clients' above it), --game-port, --for-user, --cvar.
export function netplayOptionsFromFlags(flags = {}, parseCvars = () => ({})) {
  const count = (v, d) => (v === undefined ? d : Number(v));
  const opts = {
    map: flags.map || "MAP01",
    iwad: flags.iwad || undefined,
    files: flags.file ? String(flags.file).split(",").map((f) => f.trim()).filter(Boolean) : [],
    players: count(flags.players, 1),
    observers: count(flags.observers, 0),
    lag: flags.lag ?? "",
    loss: flags.loss ?? "",
    basePort: count(flags.port, NETPLAY_BRIDGE_PORT),
    gamePort: count(flags["game-port"], NETPLAY_GAME_PORT),
    forUser: !!flags["for-user"],
    clientCvars: parseCvars(flags.cvar) || {},
  };
  const flagOf = { players: "players", observers: "observers", basePort: "port", gamePort: "game-port" };
  for (const [k, flag] of Object.entries(flagOf)) {
    if (!Number.isInteger(opts[k]) || opts[k] < 0) throw new Error(`--${flag} wants a whole number, not ${flags[flag]}`);
  }
  if (opts.players + opts.observers < 1) throw new Error("a netplay set needs at least one client (--players or --observers)");
  return opts;
}

// [rc4l] Where netplay records the running set for netdiff, netcap and netplay --quit.
export const NETPLAY_SESSION_FILE = new URL("../.netplay-session.json", import.meta.url);

export function writeNetplaySession(session, file = NETPLAY_SESSION_FILE, write = fs.writeFileSync) {
  write(file, JSON.stringify(session, null, 2));
}

// [rc4l] The running set's record, or a throw naming the command that makes one.
export function readNetplaySession(file = NETPLAY_SESSION_FILE, read = fs.readFileSync) {
  try {
    return JSON.parse(read(file, "utf8"));
  } catch {
    throw new Error("no netplay set is running (start one with fuactl netplay)");
  }
}
