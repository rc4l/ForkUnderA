// [Doruk-Aksoy] ACS scenario checks on a netgame: each step's script runs on the server as itself and on every client as a CLIENTSIDE copy that a
// server script starts, the way a mod runs clientside code, and each machine's console output is kept by step for the scenario's own judge.
import { PCD, SCRIPT, SCRIPTF } from "./acs.mjs";
import { SPECIAL } from "./doomnums.mjs";
import * as cap from "./capture.mjs";
import { addClient } from "./netplay.mjs";

// [Doruk-Aksoy] A script's CLIENTSIDE copy is its number plus COPY; START is the server script that starts any copy, its number its argument.
export const COPY = 1000, START = 30000;
// [Doruk-Aksoy] How many more tics a step waits for a machine that has printed nothing yet.
export const MORE_TICS = 105;

// [Doruk-Aksoy] The snapshot netcode's server cvars, legacy commands suppressed up to level 2 (level 3 drops client TIDs), which only a server reads.
export const SNAP_SERVER = { fua_snap: 1, fua_snap_send: 1, fua_snap_nolegacy: 2 };

// [Doruk-Aksoy] Each config with its server on the snapshot netcode, over any server cvars it sets itself.
export const onSnapshots = (configs) => configs.map((c) => ({ ...c, serverCvars: { ...SNAP_SERVER, ...(c.serverCvars || {}) } }));

// [Doruk-Aksoy] The player and an observer on a LAN and at 100 ms, the pair most ACS net scenarios run.
export const NET_CONFIGS = onSnapshots([{ name: "lan", players: 1, observers: 1 }, { name: "ping100", players: 1, observers: 1, lag: "100,100" }]);

// [Doruk-Aksoy] START's words: ACS_ExecuteAlways(its argument) on this map, which a server sends to every client for a CLIENTSIDE script, with the activator that ran START.
export function startWords() {
  return [PCD.PUSHSCRIPTVAR, 0, PCD.PUSHNUMBER, 0, PCD.LSPEC1 + 1, SPECIAL.ACS_ExecuteAlways, PCD.TERMINATE];
}

// [Doruk-Aksoy] `scripts` with each one numbered in `numbers` NET, so a player's client may run it on the server, and a CLIENTSIDE copy of it at number + COPY,
// then START. A copy's words are the original's, so a jump in it, which names an offset in the module, lands in the original's identical words and runs the same.
export function withNetCopies(scripts, numbers) {
  const net = new Set(numbers);
  const out = scripts.map((s) => (net.has(s.number) ? { ...s, flags: (s.flags || 0) | SCRIPTF.NET } : s));
  for (const n of numbers) {
    const s = scripts.find((x) => x.number === n);
    if (!s) throw new Error(`no script ${n} to copy`);
    if (n + COPY >= START) throw new Error(`script ${n}'s copy would reach START (${START})`);
    out.push({ ...s, number: n + COPY, flags: ((s.flags || 0) & ~SCRIPTF.NET) | SCRIPTF.CLIENTSIDE });
  }
  out.push({ code: startWords(), number: START, type: SCRIPT.CLOSED, flags: SCRIPTF.NET, args: 1 });
  return out;
}

// [Doruk-Aksoy] Starts for players 2 on at `spots` ([x, y] each), since a netgame's second player spawns on the first's start and telefrags it when the map has no other.
export function addPlayerStarts(m, spots) {
  spots.forEach(([x, y], i) => m.thing({ x, y, angle: 90, type: i + 2 }));
}

// [Doruk-Aksoy] A member's name in a fault: "the server", or its role.
export const who = (m) => (m.role === "server" ? "the server" : `the ${m.role === "latejoin" ? "late joiner" : `${m.role}'s client`}`);

// [Doruk-Aksoy] Runs each step through START on the clients in `on`, then on the server, and returns each machine's output by step tag; the clients go first, so the
// server's answers also show that nothing a client's copy did reached the server. A step is { script, tags, tics, clients, server }: its output goes under each of its
// tags, `tics` (default 20) is how long the server's tics wait for it, and `clients: false` or `server: false` runs it on one side alone. `by` runs both, a player's
// client making itself the activator (the scripts NET) or the server's console leaving none; `server: false` skips every server run.
export async function runSteps(set, steps, { on = set.clients, by = set.clients[0], server = true, quietMs = 400 } = {}, deps = {}) {
  const { exec = cap.exec, listen = cap.listen, waitTics = cap.waitTics } = deps;
  const sc = set.server.c;
  const logs = new Map([...on.map((m) => [m, {}]), ...(server ? [[set.server, {}]] : [])]);
  const keep = (m, step, text) => { for (const tag of step.tags) logs.get(m)[tag] = (logs.get(m)[tag] || "") + text; };
  // [Doruk-Aksoy] Waits `tics`, then up to MORE_TICS more while a machine has printed nothing, as a lagged client's script starts late.
  const run = async (members, command, tics) => {
    const heard = members.map((m) => [m, listen(m.c)]);
    await exec(by.c, command, { quietMs });
    await waitTics(sc, tics);
    for (let more = 0; more < MORE_TICS && heard.some(([, h]) => !h.text().trim()); more += 5) await waitTics(sc, 5);
    return heard.map(([m, h]) => { h.off(); return [m, h.text()]; });
  };
  for (const step of steps) {
    if (step.clients !== false) for (const [m, text] of await run(on, `puke ${START} ${step.script + COPY}`, step.tics ?? 20)) keep(m, step, text);
    if (server && step.server !== false) for (const [m, text] of await run([set.server], `puke ${step.script}`, step.tics ?? 20)) keep(m, step, text);
  }
  return logs;
}

// [Doruk-Aksoy] A scenario's judgeNetplay: `steps` on every machine, then a late joiner and `late` on it alone, each machine's faults from `judge(logs, member)`;
// `by` "player" runs them from the first client, its pawn the activator, and "server" from the server's console.
export function netJudge({ steps, late = [], judge, passed, by = "player" }, deps = {}) {
  const { join = addClient, waitTics = cap.waitTics, run = runSteps } = deps;
  return async (set, label) => {
    await waitTics(set.server.c, 35);
    const runner = by === "server" ? set.server : set.clients[0];
    const faults = judgeEach(await run(set, steps, { by: runner }), judge);
    if (late.length) {
      const joiner = await join(set, { role: "latejoin" });
      await waitTics(set.server.c, 35);
      faults.push(...judgeEach(await run(set, late, { on: [joiner], server: false, by: runner }), judge));
    }
    return result(label, faults, passed);
  };
}

// [Doruk-Aksoy] `role`'s answer for `key` from a scenario's per-machine answers, else `fallback`; "client" stands for every client's role.
export function answerFor(answers, role, key, fallback) {
  for (const r of [role, role === "server" ? null : "client"]) if (r && answers[r] && key in answers[r]) return answers[r][key];
  return fallback;
}

// [Doruk-Aksoy] Until `m` is in a level on `map`, a client connected again too, pressing use through an intermission as a player does; false when it never got there.
export async function arriveOn(m, map, { timeoutMs = 60000 } = {}, deps = {}) {
  const { exec = cap.exec, sleep = cap.sleep, now = Date.now } = deps;
  const deadline = now() + timeoutMs;
  while (now() < deadline) {
    const t = (await m.c.rpc("sim.tic").catch(() => ({}))) || {};
    if (t.gamestate === cap.GS_INTERMISSION) {
      await exec(m.c, "+use", { quietMs: 60 });
      await exec(m.c, "-use", { quietMs: 60 });
    } else if (t.inlevel && t.map === map && (m.role === "server" || (await m.c.rpc("net.state", {}).catch(() => ({})))?.connection === "active")) return true;
    await sleep(150);
  }
  return false;
}

// [Doruk-Aksoy] Takes the netgame to `map` by the server's changemap, which keeps global variables, and returns who never arrived.
export async function changeMap(set, map, deps = {}) {
  const { exec = cap.exec, waitTics = cap.waitTics, arrive = arriveOn } = deps;
  await exec(set.server.c, `changemap ${map}`, { quietMs: 400 });
  const missing = [];
  for (const m of [set.server, ...set.clients]) if (!(await arrive(m, map))) missing.push(`${who(m)} never arrived on ${map}`);
  await waitTics(set.server.c, 20);
  return missing;
}

// [Doruk-Aksoy] Each machine's faults from `judge(logs, member)`, prefixed with who it is, as one list.
export function judgeEach(logs, judge) {
  return [...logs].flatMap(([m, l]) => judge(l, m).map((f) => `${who(m)}: ${f}`));
}

// [Doruk-Aksoy] A scenario's PASS or FAIL line and failed count from its faults.
export function result(label, faults, passed) {
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${passed}`, failed: faults.length ? 1 : 0 };
}
