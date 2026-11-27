import { test } from "node:test";
import assert from "node:assert/strict";
import { PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { UdmfMap } from "../src/udmf.mjs";
import { COPY, START, MORE_TICS, onSnapshots, NET_CONFIGS, startWords, withNetCopies, addPlayerStarts, who, runSteps, netJudge, answerFor, arriveOn, changeMap, judgeEach, result } from "../src/netacs.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";
import { GS_INTERMISSION } from "../src/capture.mjs";

test("START runs ACS_ExecuteAlways on its argument, and each copied script gets a NET original and a CLIENTSIDE copy", () => {
  assert.deepEqual(startWords(), [PCD.PUSHSCRIPTVAR, 0, PCD.PUSHNUMBER, 0, PCD.LSPEC1 + 1, SPECIAL.ACS_ExecuteAlways, PCD.TERMINATE]);
  const scripts = [{ code: [1], number: 1, type: SCRIPT.CLOSED }, { code: [2], number: 2, type: SCRIPT.CLOSED, flags: SCRIPTF.BUSY }, { code: [3], name: "named", type: SCRIPT.CLOSED }];
  const out = withNetCopies(scripts, [1, 2]);
  assert.deepEqual(out.map((s) => [s.number ?? s.name, s.flags || 0]), [[1, SCRIPTF.NET], [2, SCRIPTF.BUSY | SCRIPTF.NET], ["named", 0],
    [1 + COPY, SCRIPTF.CLIENTSIDE], [2 + COPY, SCRIPTF.BUSY | SCRIPTF.CLIENTSIDE], [START, SCRIPTF.NET]]);
  assert.deepEqual(out[3].code, [1]);
  assert.equal(out[5].args, 1);
  assert.throws(() => withNetCopies(scripts, [7]), /no script 7/);
  assert.throws(() => withNetCopies([{ code: [], number: START - COPY }], [START - COPY]), /would reach START/);
});

test("players 2 on start where they are put", () => {
  const m = new UdmfMap();
  addPlayerStarts(m, [[1, 2], [3, 4]]);
  assert.deepEqual(m.things.map((t) => [t.type, t.x, t.y]), [[2, 1, 2], [3, 3, 4]]);
});

// [Doruk-Aksoy] A fake netgame whose consoles answer each command with what a machine logs, in the order the commands ran.
function fakeSet() {
  const members = ["server", "player", "observer"].map((role) => ({ role, c: { role } }));
  const set = { server: members[0], clients: members.slice(1) };
  const order = [], hearing = new Set();
  const deps = {
    listen: (c) => { const h = { c, out: "" }; hearing.add(h); return { text: () => h.out, off: () => hearing.delete(h) }; },
    exec: async (c, text) => {
      order.push(`${c.role}: ${text}`);
      const copy = / (\d+)$/.exec(text)?.[1];
      for (const h of hearing) {
        if (text.startsWith(`puke ${START}`)) { if (h.c.role !== "server") h.out += `${h.c.role} ran ${copy}\n`; }
        else if (h.c.role === "server") h.out += `server ran ${text}\n`;
      }
    },
    waitTics: async () => {},
  };
  return { set, order, deps };
}

test("each step runs on the clients through START, then on the server, its output kept by tag for every machine", async () => {
  const { set, order, deps } = fakeSet();
  const logs = await runSteps(set, [{ script: 1, tags: ["a", "b"] }, { script: 2, tags: ["c"] }], {}, deps);
  assert.deepEqual(order, [`player: puke ${START} ${1 + COPY}`, "player: puke 1", `player: puke ${START} ${2 + COPY}`, "player: puke 2"]);
  assert.deepEqual([...logs.keys()].map((m) => m.role), ["player", "observer", "server"]);
  assert.equal(logs.get(set.clients[1]).a, `observer ran ${1 + COPY}\n`);
  assert.equal(logs.get(set.clients[1]).b, `observer ran ${1 + COPY}\n`);
  assert.equal(logs.get(set.server).c, "server ran puke 2\n");
  const alone = await runSteps(set, [{ script: 1, tags: ["a"] }], { on: [set.clients[1]], server: false, by: set.server }, deps);
  assert.deepEqual([...alone.keys()].map((m) => m.role), ["observer"]);
  assert.equal(order.at(-1), `server: puke ${START} ${1 + COPY}`);
});

test("a machine arrives on a map when in its level there, a client once connected again, pressing use through an intermission", async () => {
  const replies = [{ gamestate: GS_INTERMISSION }, { inlevel: true, map: "MAP01" }, { inlevel: true, map: "MAP02" }, { inlevel: true, map: "MAP02" }];
  const states = [{ connection: "connecting" }, { connection: "active" }];
  const pressed = [];
  const c = { rpc: async (cmd) => (cmd === "sim.tic" ? replies.shift() : states.shift()) };
  const deps = { exec: async (_, t) => pressed.push(t), sleep: async () => {} };
  assert.equal(await arriveOn({ role: "player", c }, "MAP02", {}, deps), true);
  assert.deepEqual(pressed, ["+use", "-use"]);
  let clock = 0;
  const never = { role: "server", c: { rpc: async () => { throw new Error("gone"); } } };
  assert.equal(await arriveOn(never, "MAP02", { timeoutMs: 10 }, { ...deps, now: () => (clock += 6) }), false);
});

test("a changemap names whoever never arrives", async () => {
  const { set, order } = fakeSet();
  const missing = await changeMap(set, "MAP02", { exec: async (c, t) => order.push(`${c.role}: ${t}`), waitTics: async () => {}, arrive: async (m) => m.role !== "observer" });
  assert.deepEqual(order, ["server: changemap MAP02"]);
  assert.deepEqual(missing, ["the observer's client never arrived on MAP02"]);
});

test("faults name the machine, and a scenario's line is PASS or FAIL", () => {
  assert.equal(who({ role: "server" }), "the server");
  assert.equal(who({ role: "latejoin" }), "the late joiner");
  assert.equal(who({ role: "player" }), "the player's client");
  const logs = new Map([[{ role: "server" }, { a: "1" }], [{ role: "player" }, { a: "2" }]]);
  assert.deepEqual(judgeEach(logs, (l, m) => (l.a === "1" ? [] : [`${m.role} read ${l.a}`])), ["the player's client: player read 2"]);
  assert.deepEqual(result("MAP01 lan", [], "ok"), { line: "PASS MAP01 lan: ok", failed: 0 });
  assert.deepEqual(result("MAP01 lan", ["x", "y"], "ok"), { line: "FAIL MAP01 lan: x; y", failed: 1 });
});

test("a step may run on one side alone", async () => {
  const { set, order, deps } = fakeSet();
  await runSteps(set, [{ script: 3, tags: ["x"], clients: false }, { script: 4, tags: ["y"], server: false }], {}, deps);
  assert.deepEqual(order, ["player: puke 3", `player: puke ${START} ${4 + COPY}`]);
});

test("a scenario's netJudge runs the steps, then the late joiner's alone, judging every machine", async () => {
  const { set } = fakeSet();
  const late = { role: "latejoin", c: {} };
  const runs = [];
  const run = async (s, steps, opts) => { runs.push([steps.map((x) => x.script), (opts.on || s.clients).map((m) => m.role), opts.server ?? true, opts.by.role]);
    return new Map([...(opts.on || s.clients).map((m) => [m, { a: m.role }]), ...(opts.server === false ? [] : [[s.server, { a: "server" }]])]); };
  const judge = netJudge({ steps: [{ script: 1 }], late: [{ script: 2 }], judge: (l) => (l.a === "observer" ? ["off"] : []), passed: "ok" },
    { join: async (s) => { s.clients.push(late); return late; }, waitTics: async () => {}, run });
  assert.deepEqual(await judge(set, "MAP01 lan"), { line: "FAIL MAP01 lan: the observer's client: off", failed: 1 });
  assert.deepEqual(runs, [[[1], ["player", "observer"], true, "player"], [[2], ["latejoin"], false, "player"]]);
  const byServer = netJudge({ steps: [{ script: 1 }], judge: () => [], passed: "ok", by: "server" }, { waitTics: async () => {}, run });
  assert.deepEqual(await byServer(fakeSet().set, "M"), { line: "PASS M: ok", failed: 0 });
  assert.equal(runs.at(-1)[3], "server");
});

test("a machine's answer is its role's, then any client's, then the fallback", () => {
  const answers = { client: { a: 1, b: 2 }, latejoin: { b: 3 } };
  assert.deepEqual(["server", "player", "latejoin"].map((r) => [answerFor(answers, r, "a", 0), answerFor(answers, r, "b", 0)]), [[0, 0], [1, 2], [1, 3]]);
  assert.equal(answerFor(answers, "player", "c", 9), 9);
});

test("a step waits on, at most MORE_TICS, for a machine whose output comes late", async () => {
  const { set, deps } = fakeSet();
  let waits = 0, late = null;
  const slow = { ...deps,
    listen: (c) => { const h = deps.listen(c); if (c.role === "observer") { late = { c, text: h.text }; return { text: () => (waits >= 4 ? "observer ran late\n" : ""), off: h.off }; } return h; },
    waitTics: async () => { waits++; } };
  const logs = await runSteps(set, [{ script: 1, tags: ["a"], server: false }], {}, slow);
  assert.equal(logs.get(set.clients[1]).a, "observer ran late\n");
  assert.equal(waits, 4);
  waits = -1000;
  await runSteps(set, [{ script: 1, tags: ["a"], server: false }], {}, slow);
  assert.equal(waits, -1000 + 1 + MORE_TICS / 5);
  assert.ok(late);
});

test("onSnapshots puts every config's server on the snapshot netcode, keeping its own server cvars and its clients' as they are", () => {
  const [plain, own] = onSnapshots([{ name: "a" }, { name: "b", serverCvars: { sv_cheats: 0, fua_snap_nolegacy: 1 }, clientCvars: { x: 1 } }]);
  assert.deepEqual(plain.serverCvars, SNAP_SERVER_CVARS);
  assert.deepEqual(own.serverCvars, { fua_snap: 1, fua_snap_send: 1, fua_snap_nolegacy: 1, sv_cheats: 0 });
  assert.deepEqual(own.clientCvars, { x: 1 });
  assert.deepEqual(NET_CONFIGS.map((c) => [c.name, c.observers, c.serverCvars]), [["lan", 1, SNAP_SERVER_CVARS], ["ping100", 1, SNAP_SERVER_CVARS]]);
});

test("after the late joiner's round, `after` runs on every machine with it, and each machine is judged once on its merged output", async () => {
  const { set } = fakeSet();
  const late = { role: "latejoin", c: {} };
  const runs = [];
  const run = async (s, steps, opts) => {
    const on = opts.on || s.clients;
    runs.push([steps.map((x) => x.script), on.map((m) => m.role), opts.server ?? true]);
    return new Map([...on.map((m) => [m, { [`t${steps[0].script}`]: m.role }]), ...(opts.server === false ? [] : [[s.server, { [`t${steps[0].script}`]: "server" }]])]);
  };
  const judged = [];
  const judge = netJudge({ steps: [{ script: 1 }], late: [{ script: 2 }], after: [{ script: 3 }], judge: (l, m) => { judged.push([m.role, Object.keys(l).sort().join()]); return []; }, passed: "ok" },
    { join: async (s) => { s.clients.push(late); return late; }, waitTics: async () => {}, run });
  assert.deepEqual(await judge(set, "M"), { line: "PASS M: ok", failed: 0 });
  assert.deepEqual(runs, [[[1], ["player", "observer"], true], [[2], ["latejoin"], false], [[3], ["player", "observer", "latejoin"], true]]);
  assert.deepEqual(judged, [["player", "t1,t3"], ["observer", "t1,t3"], ["server", "t1,t3"], ["latejoin", "t2,t3"]]);
});

test("netJudge readies the set with `before` once the level settled, before any step", async () => {
  const { set } = fakeSet();
  const order = [];
  const judge = netJudge({ steps: [{ script: 1 }], judge: () => [], passed: "ok", before: async (s) => order.push(["before", s === set]) },
    { waitTics: async () => order.push(["wait"]), run: async () => { order.push(["run"]); return new Map(); } });
  await judge(set, "M");
  assert.deepEqual(order, [["wait"], ["before", true], ["run"]]);
});
