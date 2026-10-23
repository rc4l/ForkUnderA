import { test } from "node:test";
import assert from "node:assert/strict";
import { serverArgs, clientArgs, clientCvars, perClient, clientRoles, clientPlaying, launchNetplay, addClient, joinThroughBrowser, seamViolations, replicationMisses, fireUntilDead, joinGame, closeNetplay, netplaySession, netplayOptionsFromFlags, NETPLAY_BRIDGE_PORT, NETPLAY_GAME_PORT, writeNetplaySession, readNetplaySession, connectionTrouble, withTrouble } from "../src/netplay.mjs";

test("a client's log gives the reason it never got in, and an error carries it", () => {
  assert.equal(connectionTrouble("Connecting to 127.0.0.1:10840\r\nToo many connections from your IP.\r\nupdate check"), "Too many connections from your IP.");
  assert.match(connectionTrouble("CLIENT_CheckForMissingPackets: Missing more than 2048 packets (last parsed 3, highest received 9000). Unable to recover."), /Unable to recover/);
  assert.equal(connectionTrouble("Connected!\nLevel authenticated!"), null);
  assert.equal(connectionTrouble(undefined), null);
  assert.equal(withTrouble(new Error("never reached GS_LEVEL"), "Server is full.").message, 'never reached GS_LEVEL: the client says "Server is full."');
  const e = new Error("x");
  assert.equal(withTrouble(e, "fine"), e);
});

test("a client that fails to come up is named with its log's reason", async () => {
  const set = { basePort: 7900, gamePort: 10950, iwad: "w", files: [], server: { c: { rpc: async () => ({}) } }, clients: [] };
  const deps = { launch: async () => ({ port: 7901, log: "x.log" }), connect: async () => ({ rpc: async () => ({}), close() {} }),
    waitInLevel: async () => { throw new Error("instance never reached GS_LEVEL"); }, readLog: () => "Too many connections from your IP.", timeoutMs: 1000 };
  await assert.rejects(addClient(set, { role: "latejoin" }, deps), /never reached GS_LEVEL: the client says "Too many connections from your IP\."/);
});

test("a server hosts on its game port on loopback with no announcing and cheats from launch", () => {
  assert.deepEqual(serverArgs({ files: ["a.wad"], gamePort: 10900 }),
    ["-file", "a.wad", "-host", "-port", "10900", "+sv_broadcast", "0", "+sv_updatemaster", "0", "+sv_cheats", "1", "+sv_maxclientsperip", "0", "-useip", "127.0.0.1"]);
  assert.equal(serverArgs().includes("-file"), false);
  assert.equal(serverArgs({ broadcast: true }).includes("+sv_broadcast"), false);
});

test("a client joins on loopback, with WebGPU when asked", () => {
  assert.deepEqual(clientArgs({ files: ["a.wad"] }), ["-file", "a.wad", "-useip", "127.0.0.1"]);
  assert.deepEqual(clientArgs({ webgpu: true }), ["-useip", "127.0.0.1", "-webgpu"]);
});

test("a client presents unfocused, asks no public registry and takes its fake ping and loss only when set", () => {
  const quiet = { fua_render_in_background: 1, cl_fua_serverregistry_list: "", cl_fua_serverregistrylist_fetch: 0 };
  assert.deepEqual(clientCvars(), quiet);
  assert.deepEqual(clientCvars({ lag: 100, loss: 5, cvars: { cl_unlagged: 1 } }), { ...quiet, fua_net_fakeping: 100, fua_net_fakeloss: 5, cl_unlagged: 1 });
});

test("per-client values spread from a comma list", () => {
  assert.deepEqual(perClient("100,0", 3), [100, 0, 0]);
  assert.deepEqual(perClient("50", 2), [50, 50]);
  assert.deepEqual(perClient("", 2), [0, 0]);
  assert.deepEqual(perClient(undefined, 1), [0]);
  assert.throws(() => perClient("a,1", 2), /comma-separated numbers/);
  assert.throws(() => perClient("-5", 1), /comma-separated numbers/);
});

test("players launch before observers and a late joiner last", () => {
  assert.deepEqual(clientRoles({ players: 2, observers: 1 }), ["player", "player", "observer"]);
  assert.deepEqual(clientRoles({ players: 1, lateJoin: true }), ["player", "latejoin"]);
  assert.deepEqual(clientRoles(), ["player"]);
});

test("a client plays once the server holds it spawned and not spectating", () => {
  const state = { clients: [{ player: 1, state: "spawned", spectating: false }, { player: 2, state: "spawned", spectating: true }, { player: 3, state: "connected", spectating: false }] };
  assert.equal(clientPlaying(state, 1), true);
  assert.equal(clientPlaying(state, 2), false);
  assert.equal(clientPlaying(state, 3), false);
  assert.equal(clientPlaying(state, 4), false);
  assert.equal(clientPlaying(null, 1), false);
});

// [rc4l] Fake engines: each launch an instance, each connection answering net.state as a joined client would.
function fakes({ failOn = -1 } = {}) {
  const launched = [], execs = [], stopped = [], closed = [];
  let n = 0;
  const players = new Map();
  const launch = async (opts) => {
    if (n === failOn) { n++; throw new Error("launch failed"); }
    const inst = { port: opts.port, token: `t${n}`, pid: 100 + n, opts };
    n++;
    launched.push(inst);
    return inst;
  };
  const rpcs = [];
  let browserAnswersAfter = 0;
  const connect = async (inst) => ({
    inst,
    rpc: async (m, args) => {
      rpcs.push([inst.port, m, args]);
      if (m === "browser.list") return { servers: [{ address: "127.0.0.1:10950", name: browserAnswersAfter-- > 0 ? "" : "srv", lan: true }] };
      if (m !== "net.state") return {};
      if (inst.opts.extraArgs.includes("-host")) return { role: "server", clients: [...players.entries()].map(([p, joined]) => ({ player: p, state: joined ? "spawned" : "connected", spectating: !joined })) };
      const p = launched.indexOf(inst);
      if (!players.has(p)) players.set(p, false);
      return { role: "client", connection: "active", consoleplayer: p };
    },
    close: () => closed.push(inst.port),
  });
  const exec = async (c, text) => { execs.push([c.inst.port, text]); if (text === "join") players.set(launched.indexOf(c.inst), true); return ""; };
  const deps = { launch, connect, exec, waitInLevel: async () => {}, sleep: async () => {}, stop: async (i) => stopped.push(i.port), timeoutMs: 1000 };
  return { deps, launched, execs, stopped, closed, rpcs, answerAfter: (n) => { browserAnswersAfter = n; } };
}

test("a set launches the server in the map, then each client joined with its own ping", async () => {
  const f = fakes();
  const set = await launchNetplay({ iwad: "freedoom2.wad", map: "MAP01", files: ["x.wad"], players: 1, observers: 1, lag: "100,0", basePort: 7900, gamePort: 10950 }, f.deps);
  assert.equal(f.launched.length, 3);
  assert.equal(f.launched[0].opts.map, "MAP01");
  assert.ok(f.launched[0].opts.extraArgs.includes("-host"));
  assert.equal(f.launched[1].opts.connect, "127.0.0.1:10950");
  assert.equal(f.launched[1].opts.port, 7901);
  assert.equal(f.launched[1].opts.cvars.fua_net_fakeping, 100);
  assert.equal(f.launched[2].opts.cvars.fua_net_fakeping, undefined);
  assert.deepEqual(set.clients.map((c) => c.role), ["player", "observer"]);
  assert.deepEqual(f.execs.map((e) => e[1]), ["join", "join"]);
  const session = netplaySession(set);
  assert.equal(session.server.port, 7900);
  assert.deepEqual(session.clients.map((c) => [c.role, c.port, c.lag, c.player]), [["player", 7901, 100, 1], ["observer", 7902, 0, 2]]);
  await closeNetplay(set, f.deps.stop);
  assert.deepEqual(f.stopped, [7902, 7901, 7900]);
});

test("a late joiner joins a running set, and a set that fails to launch is torn down", async () => {
  const f = fakes();
  const set = await launchNetplay({ players: 1, basePort: 7900 }, f.deps);
  const late = await addClient(set, { role: "latejoin" }, f.deps);
  assert.equal(late.inst.port, 7902);
  assert.equal(set.clients.length, 2);
  const g = fakes({ failOn: 1 });
  await assert.rejects(launchNetplay({ players: 1, basePort: 7900 }, g.deps), /launch failed/);
  assert.deepEqual(g.stopped, [7900]);
});

test("clients added at once each get their own port, and a browser join fetches the server's wads itself", async () => {
  const f = fakes();
  const set = await launchNetplay({ files: ["x.wad"], players: 0, basePort: 7900, gamePort: 10950 }, f.deps);
  f.answerAfter(2);
  const [a, b] = await Promise.all([addClient(set, { files: [], via: "browser" }, f.deps), addClient(set, {}, f.deps)]);
  assert.deepEqual([a.inst.port, b.inst.port], [7901, 7902]);
  assert.equal(a.inst.opts.connect, undefined);
  assert.ok(!a.inst.opts.extraArgs.includes("x.wad") && b.inst.opts.extraArgs.includes("x.wad"));
  assert.deepEqual(f.rpcs.filter((r) => r[0] === 7901 && r[1].startsWith("browser.")).map((r) => [r[1], r[2]?.address]),
    [["browser.add", "127.0.0.1:10950"], ["browser.list", undefined], ["browser.list", undefined], ["browser.list", undefined], ["browser.select", "127.0.0.1:10950"]]);
  assert.deepEqual(f.execs.filter((e) => e[0] === 7901).map((e) => e[1]), ["openmenu ZA_Browser", "fua_join_selected_server", "join"]);
  const lan = await launchNetplay({ players: 0, broadcast: true, cvars: { sv_broadcast: 0 }, basePort: 7900, gamePort: 10950 }, f.deps);
  assert.equal(f.launched.at(-1).opts.cvars.sv_broadcast, 0);
  await addClient(lan, { via: "lan" }, f.deps);
  assert.equal(f.rpcs.filter((r) => r[0] === 7901 && r[1] === "browser.add").length, 1, "a LAN join adds nothing by address");
  assert.match(connectionTrouble("Can't find 'm.wad'\nDownloading 2 files to C:/t/\nDownloading m.wad from 127.0.0.1:10950"), /^Downloading 2 files to/);
});

test("a browser that never hears from the server says so", async () => {
  const f = fakes();
  f.answerAfter(1e9);
  const c = await f.deps.connect({ port: 1, opts: { extraArgs: [] } });
  await assert.rejects(joinThroughBrowser(c, "127.0.0.1:10950", { ...f.deps, timeoutMs: 0 }), /the browser never heard back from 127\.0\.0\.1:10950/);
  const queried = { inst: { port: 1 }, rpc: async (m) => (m === "browser.list" ? { servers: [{ address: "127.0.0.1:10950", name: "srv", lan: false }] } : {}) };
  await assert.rejects(joinThroughBrowser(queried, "127.0.0.1:10950", { ...f.deps, timeoutMs: 0 }, { lan: true }), /never heard 127\.0\.0\.1:10950 announce itself on the LAN/);
  f.answerAfter(0);
  const stuck = { inst: { port: 1 }, rpc: async (m) => (m === "browser.list" ? { servers: [{ address: "127.0.0.1:10950", name: "srv" }] } : m === "net.state" ? { connection: "disconnected" } : {}) };
  await assert.rejects(joinThroughBrowser(stuck, "127.0.0.1:10950", { ...f.deps, timeoutMs: 0 }), /never connected to 127\.0\.0\.1:10950 after joining from the browser/);
});

test("each instance whose netcode seams another thread entered is named", async () => {
  const set = { server: { c: "s", role: "server", player: -1 }, clients: [{ c: "a", role: "player", player: 0 }, { c: "b", role: "observer", player: 1 }, { c: null, role: "latejoin" }] };
  const said = { s: "0 violations; 0 locks taken while holding another", a: "net/parse  main  9 entries  1 violations\n1 violations, the first in net/parse on the game thread in CLIENT_ParsePacket:1430; 0 locks", b: "Unknown command" };
  assert.deepEqual(await seamViolations(set, async (c) => said[c]), ["player player 0: 1 seam violations, the first in net/parse (game, CLIENT_ParsePacket:1430)"]);
});

test("a server that sent a change late, untouched, is named with the first", () => {
  assert.equal(replicationMisses("replication: 40 rows sent, 0 missed"), "");
  assert.equal(replicationMisses("replication: 40 rows sent, 2 missed, the first line 7 (families 4)"), "the server sent 2 changed rows a tic late, untouched, the first line 7 (families 4)");
  assert.equal(replicationMisses("Unknown command"), "");
});

test("fire is held until the server holds the first live target dead, and let go however it ends", async () => {
  const execs = [];
  const exec = async (c, text) => execs.push(text);
  const quick = { exec, sleep: async () => {} };
  let health = 20;
  const corpse = { c: "ZombieMan", netid: 3, health: -5 };
  const server = { rpc: async () => ({ actors: [corpse, { c: "DoomImp", netid: 4, health: 60 }, { c: "ZombieMan", netid: 5, health: (health -= 10) }] }) };
  assert.equal(await fireUntilDead("p", server, /zombie/i, quick), 5);
  assert.deepEqual(execs, ["+attack", "-attack"]);
  let t = 0;
  const alive = { rpc: async () => ({ actors: [{ c: "ZombieMan", netid: 7, health: 20 }] }) };
  assert.equal(await fireUntilDead("p", alive, /zombie/i, { ...quick, seconds: 1, now: () => (t += 600) }), null);
  assert.equal(execs.at(-1), "-attack");
  let calls = 0;
  const vanishes = { rpc: async () => ({ actors: calls++ ? [] : [{ c: "ZombieMan", netid: 8, health: 20 }] }) };
  assert.equal(await fireUntilDead("p", vanishes, /zombie/i, quick), null);
  execs.length = 0;
  assert.equal(await fireUntilDead("p", { rpc: async () => ({ actors: [corpse] }) }, /zombie/i, quick), null);
  assert.equal(await fireUntilDead("p", { rpc: async () => ({}) }, /zombie/i, quick), null);
  assert.deepEqual(execs, [], "nothing to kill fires nothing");
  health = 20;
  assert.equal(await fireUntilDead("p", server, (a) => a.netid === 5, quick), 5);
});

test("a join is waited on until the server holds the client playing, or reported not to have happened", async () => {
  const execs = [];
  const exec = async (c, text) => execs.push(text);
  let polls = 0;
  const client = { c: { rpc: async () => ({ consoleplayer: 2, connection: polls++ ? "active" : "attempting" }) }, player: -1 };
  const server = { rpc: async () => ({ clients: [{ player: 2, state: "spawned", spectating: false }] }) };
  const r = await joinGame(client, server, { exec, sleep: async () => {}, timeoutMs: 60000 });
  assert.deepEqual([r.joined, client.player, execs], [true, 2, ["join"]]);
  const watching = { rpc: async () => ({ clients: [{ player: 2, state: "spawned", spectating: true }] }) };
  const never = await joinGame({ c: { rpc: async () => null }, player: 0 }, watching, { exec, sleep: async () => {}, timeoutMs: 0 });
  assert.deepEqual([never.joined, never.mine], [false, null]);
  // [rc4l] A join lost to packet loss is asked again once resendMs has passed, and not before.
  execs.length = 0;
  let t = 0, held = 0;
  const lateServer = { rpc: async () => ({ clients: [{ player: 2, state: "spawned", spectating: held++ < 6 }] }) };
  const active = { c: { rpc: async () => ({ consoleplayer: 2, connection: "active" }) }, player: -1 };
  const again = await joinGame(active, lateServer, { exec, sleep: async () => { t += 1000; }, timeoutMs: 60000, resendMs: 3500, now: () => t });
  assert.deepEqual([again.joined, execs], [true, ["join", "join"]]);
});

test("a client the server never holds playing times out", async () => {
  const f = fakes();
  f.deps.exec = async () => "";
  await assert.rejects(launchNetplay({ players: 1 }, { ...f.deps, timeoutMs: 0 }), /never joined the game/);
});

test("a command line's netplay options, with the defaults and the bad values refused", () => {
  const o = netplayOptionsFromFlags({ map: "MAP02", file: "a.wad, b.pk3", players: "2", observers: "1", lag: "100,0", "for-user": true, cvar: "x=1" }, () => ({ x: "1" }));
  assert.deepEqual([o.map, o.files, o.players, o.observers, o.lag, o.forUser, o.clientCvars], ["MAP02", ["a.wad", "b.pk3"], 2, 1, "100,0", true, { x: "1" }]);
  const d = netplayOptionsFromFlags({});
  assert.deepEqual([d.map, d.files, d.players, d.observers, d.basePort, d.gamePort, d.forUser], ["MAP01", [], 1, 0, NETPLAY_BRIDGE_PORT, NETPLAY_GAME_PORT, false]);
  assert.throws(() => netplayOptionsFromFlags({ players: "two" }), /--players wants a whole number, not two/);
  assert.throws(() => netplayOptionsFromFlags({ port: "-1" }), /--port wants a whole number/);
  assert.throws(() => netplayOptionsFromFlags({ players: "0" }), /at least one client/);
});

test("the running set is recorded and read back, and a missing record names the command that makes one", () => {
  let held = null;
  writeNetplaySession({ gamePort: 1 }, "f", (f, text) => { held = text; });
  assert.deepEqual(readNetplaySession("f", () => held), { gamePort: 1 });
  assert.throws(() => readNetplaySession("f", () => { throw new Error("ENOENT"); }), /start one with fuactl netplay/);
});
