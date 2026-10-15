import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { consoleTextFor, soakStep, parseConsoleJson, runSoak, compareStats, formatCompare, baselinePath, perfGate, execSlow } from "../src/telemetry.mjs";

test("each view is the console command it names, built from its words and flags", () => {
  assert.equal(consoleTextFor("console", ["map", "MAP02"]), "map MAP02");
  assert.equal(consoleTextFor("stats", []), "fua_stats");
  assert.equal(consoleTextFor("stats", ["gpu/"], { json: true }), "fua_stats json gpu/");
  assert.equal(consoleTextFor("stats", ["mem/"], { watch: true }), "fua_stats watch mem/");
  assert.equal(consoleTextFor("hitches", []), "fua_hitches 10");
  assert.equal(consoleTextFor("hitches", ["3"]), "fua_hitches 3");
  assert.equal(consoleTextFor("flight", []), "fua_flightdump");
  assert.equal(consoleTextFor("flight", ["q48"]), "fua_flightdump q48");
  assert.equal(consoleTextFor("memsnap", ["before"]), "fua_memsnap before");
  assert.equal(consoleTextFor("memdiff", ["a", "b", "c"]), "fua_memdiff a b");
  assert.equal(consoleTextFor("play", []), null);
});

test("a soak step is a named action on the soak's map, or the console text given", () => {
  assert.deepEqual(soakStep("reload", "MAP11"), { text: "map MAP11", newLevel: true });
  assert.deepEqual(soakStep("vid_setmode 800 600", "MAP11"), { text: "vid_setmode 800 600", newLevel: false });
});

test("the JSON a console reply holds is found among its other lines, strings with brackets included", () => {
  assert.deepEqual(parseConsoleJson('telemetry: x\n[{"name":"a]b","v":1}]\nmore'), [{ name: "a]b", v: 1 }]);
  assert.deepEqual(parseConsoleJson('{"pass":true,"s":"q\\"}"}'), { pass: true, s: 'q"}' });
  assert.throws(() => parseConsoleJson("no json here"), /no JSON in/);
  assert.throws(() => parseConsoleJson('[{"a":1}'), /unterminated JSON/);
});

// [rc4l] An instance that answers the soak's RPCs and console commands from a script, its level clock reset by each map command.
function fakeInstance({ verdict, leaveLevel = false, neverReturns = false }) {
  const ran = [];
  let leveltime = 500, inlevel = true;
  const c = {
    rpc: async (cmd) => {
      if (cmd === "player.camera") return { map: "MAP07" };
      if (neverReturns) return { inlevel: false, leveltime: 0 };
      const t = { inlevel, leveltime };
      inlevel = true;
      leveltime += 35;
      return t;
    },
  };
  const exec = async (_, text) => {
    ran.push(text);
    if (text.startsWith("map ")) { if (leaveLevel) inlevel = false; else leveltime = 0; }
    if (text.endsWith("json")) return `telemetry\n${JSON.stringify(verdict)}\n`;
    return "";
  };
  return { c, exec, ran };
}

test("a soak repeats its action, waits for each new level, marks after each and returns the verdict", async () => {
  const { c, exec, ran } = fakeInstance({ verdict: { pass: true, marks: 2, judged: 9, grew: [], report: "SOAK PASS\n" } });
  const logged = [];
  const r = await runSoak(c, { cycles: 2, warmup: 0, settleMs: 0, exec, sleep: async () => {}, log: (m) => logged.push(m) });
  assert.deepEqual(ran, ["fua_soak begin", "map MAP07", "fua_soak mark", "map MAP07", "fua_soak mark", "fua_soak verdict 0 json"]);
  assert.deepEqual(logged, ["cycle 1/2", "cycle 2/2"]);
  assert.equal(r.pass, true);
  assert.equal(r.report, "SOAK PASS\n");
});

test("a level left and entered again counts as the new level, and console text needs no level", async () => {
  const left = fakeInstance({ verdict: { pass: false } });
  await runSoak(left.c, { cycles: 1, exec: left.exec, sleep: async () => {} });
  assert.ok(left.ran.includes("map MAP07"));
  const text = fakeInstance({ verdict: { pass: true } });
  await runSoak(text.c, { action: "screenshot", cycles: 1, exec: text.exec, sleep: async () => {} });
  assert.ok(text.ran.includes("screenshot"));
});

test("a level that never comes back fails the soak", async () => {
  const stuck = fakeInstance({ verdict: { pass: true }, neverReturns: true });
  const realNow = Date.now;
  let now = 0;
  Date.now = () => (now += 30000);
  try {
    await assert.rejects(runSoak(stuck.c, { cycles: 1, exec: stuck.exec, sleep: async () => {} }), /the level never came back/);
  } finally { Date.now = realNow; }
});

const row = (name, unit, p50, p99, available = true) => ({ name, unit, p50, p99, available });

test("a timing regresses when its p50 or p99 grows past both the share and the floor", () => {
  const before = [row("frame/ms", "ms", 2.0, 4.0), row("draw/world", "ms", 0.2, 0.3), row("mem/heap_bytes", "bytes", 100, 100), row("gone", "ms", 1, 1), row("off", "ms", 1, 1, false)];
  const after = [row("frame/ms", "ms", 2.1, 5.0), row("draw/world", "ms", 0.28, 0.3), row("mem/heap_bytes", "bytes", 500, 500), row("new", "ms", 9, 9), row("off", "ms", 9, 9)];
  const rows = compareStats(before, after);
  assert.deepEqual(rows.map((r) => r.name), ["mem/heap_bytes", "frame/ms", "draw/world"]);
  assert.equal(rows.find((r) => r.name === "frame/ms").regressed, true);
  assert.equal(rows.find((r) => r.name === "draw/world").regressed, false);
  assert.equal(rows.find((r) => r.name === "mem/heap_bytes").regressed, false);
  assert.equal(compareStats(before, after, { share: 0.5, floor: 2 }).some((r) => r.regressed), false);
});

test("a comparison prints what changed, regressions marked, or says nothing changed", () => {
  const rows = compareStats([row("frame/ms", "ms", 2, 4), row("same", "ms", 1, 1), row("big", "bytes", 123456, 0)], [row("frame/ms", "ms", 2.5, 40), row("same", "ms", 1, 1), row("big", "bytes", 12345.6, 0)]);
  const lines = formatCompare(rows);
  assert.equal(lines.length, 3);
  assert.match(lines[0], /^counter\s+unit\s+p50 before -> after/);
  assert.match(lines.find((l) => l.startsWith("frame/ms")), /2\.00 -> 2\.50\s+4\.00 -> 40\.0  REGRESSED$/);
  assert.match(lines.find((l) => l.startsWith("big")), /123456 -> 12346/);
  assert.deepEqual(formatCompare(compareStats([row("a", "ms", 1, 1)], [row("a", "ms", 1, 1)])), ["(no change)"]);
  assert.equal(formatCompare(rows, { limit: 1 }).length, 2);
});

test("a baseline lives per machine under a plain name", () => {
  assert.equal(baselinePath("sunder-map10", "/somewhere"), path.join("/somewhere", ".forkundera", "baselines", "sunder-map10.json"));
  assert.throws(() => baselinePath("../escape"), /baseline names are/);
});

test("the first gate saves its baseline, later ones pass or fail against it, and --update saves again", () => {
  const home = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-gate-"));
  try {
    const base = [row("frame/ms", "ms", 2, 4)];
    const first = perfGate("g", base, { home });
    assert.ok(first.saved && first.pass);
    assert.equal(perfGate("g", [row("frame/ms", "ms", 2.05, 4.05)], { home }).pass, true);
    const slow = perfGate("g", [row("frame/ms", "ms", 3, 6)], { home });
    assert.equal(slow.pass, false);
    assert.equal(slow.rows[0].regressed, true);
    assert.ok(perfGate("g", [row("frame/ms", "ms", 3, 6)], { home, update: true }).saved);
    assert.equal(perfGate("g", [row("frame/ms", "ms", 3, 6)], { home }).pass, true);
  } finally { fs.rmSync(home, { recursive: true, force: true }); }
});

test("a slow command returns everything the console printed while it ran", async () => {
  let listener = null;
  const c = {
    onEvent: (fn) => { listener = fn; return () => { listener = null; }; },
    rpc: async (cmd, args) => { listener("out", { text: `ran ${args.text}.` }); return {}; },
  };
  assert.equal(await execSlow(c, "fua_stats json"), "ran fua_stats json.");
  assert.equal(listener, null);
});
