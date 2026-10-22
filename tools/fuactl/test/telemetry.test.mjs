import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { allocGrowth, parseAllocReport, consoleTextFor, levelStarted, resizeAsked, soakSteps, soakSlower, soakCycleLines, parseConsoleJson, runSoak, compareStats, formatCompare, baselinePath, perfGate, execSlow, medianReadings } from "../src/telemetry.mjs";

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

test("a soak's steps are a named action on the soak's map, or console texts separated by bars, each with the map it starts", () => {
  assert.deepEqual(soakSteps("reload", "MAP11"), [{ text: "map MAP11", map: "MAP11" }]);
  assert.deepEqual(soakSteps("vid_setmode 800 600", "MAP11"), [{ text: "vid_setmode 800 600", map: null }]);
  assert.deepEqual(soakSteps("wad_reload a.wad", undefined), [{ text: "wad_reload a.wad", map: null }]);
  assert.throws(() => soakSteps("reload", undefined), /reload repeats the current map, and the instance is in none/);
  assert.deepEqual(soakSteps("wad_reload a.wad map=E1M1 | wad_reload - b.pk3 map=MAP01|", "MAP11"),
    [{ text: "wad_reload a.wad map=E1M1", map: "E1M1" }, { text: "wad_reload - b.pk3 map=MAP01", map: "MAP01" }]);
});

test("the map console text starts is read from each command that starts one, and a connect's is the server's", () => {
  assert.equal(levelStarted("map MAP11"), "MAP11");
  assert.equal(levelStarted(" changemap e1m2"), "e1m2");
  assert.equal(levelStarted("wad_reload doom2.wad a.pk3 map=MAP03"), "MAP03");
  assert.equal(levelStarted("wad_reload doom2.wad"), null);
  assert.equal(levelStarted("fua_hostmap MAP07 name test"), "MAP07");
  assert.equal(levelStarted("connect 127.0.0.1:10670"), "*");
  assert.equal(levelStarted("disconnect"), null);
  assert.equal(levelStarted("screenshot"), null);
});

const cycle = (stepSeconds, frameMs) => ({ stepSeconds, frameMs });

test("a soak got slower when its later cycles' median passes the earlier ones' by the share and the floor", () => {
  const flat = [cycle(9, 9), cycle(1.6, 0.25), cycle(1.7, 0.26), cycle(1.6, 0.25), cycle(1.65, 0.27)];
  assert.deepEqual(soakSlower(flat, 1), []);
  const worse = [cycle(9, 9), cycle(1.6, 0.25), cycle(1.6, 0.25), cycle(2.4, 0.5), cycle(2.5, 0.6)];
  assert.deepEqual(soakSlower(worse, 1), ["stepSeconds went from 1.60 to 2.45", "frameMs went from 0.25 to 0.55"]);
  assert.deepEqual(soakSlower([cycle(0.1, 0.01), cycle(0.1, 0.01), cycle(0.2, 0.05), cycle(0.2, 0.05)], 0), []);
  assert.deepEqual(soakSlower(worse.slice(0, 4), 1), []);
  assert.deepEqual(soakCycleLines([cycle(1.5, 0.25)]), ["cycle 1: steps 1.50 s, frame p50 0.250 ms"]);
});

test("the JSON a console reply holds is found among its other lines, strings with brackets included", () => {
  assert.deepEqual(parseConsoleJson('telemetry: x\n[{"name":"a]b","v":1}]\nmore'), [{ name: "a]b", v: 1 }]);
  assert.deepEqual(parseConsoleJson('{"pass":true,"s":"q\\"}"}'), { pass: true, s: 'q"}' });
  assert.throws(() => parseConsoleJson("no json here"), /no JSON in/);
  assert.throws(() => parseConsoleJson('[{"a":1}'), /unterminated JSON/);
  // [rc4l] A level upload's bracketed summary and a pickup's message ahead of the reply are skipped.
  assert.deepEqual(parseConsoleJson('vulkan: uploaded 12 verts [lightmode 3, fogmode 1]\nArmor Essence\n[{"name":"gpu/repaint_sent","mean":2}]'), [{ name: "gpu/repaint_sent", mean: 2 }]);
  assert.throws(() => parseConsoleJson("[lightmode 3] and nothing else"), /no JSON in/);
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
    if (levelStarted(text)) { if (leaveLevel) inlevel = false; else leveltime = 0; }
    if (text.startsWith("fua_stats json")) return '[{"name":"frame/msx","p50":9},{"name":"frame/ms","p50":0.25}]';
    if (text.endsWith("json")) return `telemetry\n${JSON.stringify(verdict)}\n`;
    return "";
  };
  return { c, exec, ran };
}

test("a soak repeats its action, waits for each new level, marks after each and returns the verdict", async () => {
  const { c, exec, ran } = fakeInstance({ verdict: { pass: true, marks: 2, judged: 9, grew: [], report: "SOAK PASS\n" } });
  const logged = [];
  let t = 0;
  const r = await runSoak(c, { cycles: 2, warmup: 0, settleMs: 0, exec, sleep: async () => {}, now: () => (t += 500), log: (m) => logged.push(m) });
  assert.deepEqual(ran, ["fua_soak begin", "map MAP07", "fua_soak mark", "fua_stats json frame/ms", "map MAP07", "fua_soak mark", "fua_stats json frame/ms", "fua_soak verdict 0 json"]);
  assert.deepEqual(logged, ["cycle 1: steps 0.50 s, frame p50 0.250 ms", "cycle 2: steps 0.50 s, frame p50 0.250 ms"]);
  assert.equal(r.pass, true);
  assert.equal(r.report, "SOAK PASS\n");
  assert.deepEqual(r.cycles, [cycle(0.5, 0.25), cycle(0.5, 0.25)]);
  assert.deepEqual(r.slower, []);
});

// [rc4l] A wad_reload to the title leaves no level, so the next step's level counts as new however far its clock has run.
test("from the title any level that appears is the new one", async () => {
  const ran = [];
  let inlevel = false;
  const c = { rpc: async (cmd) => (cmd === "player.camera" ? {} : { inlevel, leveltime: inlevel ? 900 : 0 }) };
  const exec = async (_, text) => {
    ran.push(text);
    if (text.startsWith("fua_stats")) return '[{"name":"frame/ms","p50":0.2}]';
    if (text.endsWith("json")) return '{"pass":true}';
    inlevel = levelStarted(text) !== null;
    return "";
  };
  const r = await runSoak(c, { action: "wad_reload a.wad map=MAP01|wad_reload b.wad", cycles: 2, settleMs: 0, ignore: ["mem/private_bytes", "gpu/"], exec, sleep: async () => {} });
  assert.equal(r.pass, true);
  assert.equal(ran.at(-1), "fua_soak verdict 1 -mem/private_bytes -gpu/ json");
  assert.deepEqual(ran.filter((t) => t.startsWith("wad_reload")), ["wad_reload a.wad map=MAP01", "wad_reload b.wad", "wad_reload a.wad map=MAP01", "wad_reload b.wad"]);
});

const report = (lines) => `telemetry: N allocations are live; the stacks holding the most:\n${lines.join("\n")}\n`;

test("an allocation report is read by stack, and the stacks that grew are ranked", () => {
  const first = parseAllocReport(report(["  832 (46 KB)  C_CreateCVar (c_cvars.cpp:1737) <- userinfo_t::Reset", "  5 (1.5 MB)  NETBUFFER_s::Init <- CLIENT_Construct", "not a stack"]));
  assert.deepEqual(first.get("NETBUFFER_s::Init <- CLIENT_Construct"), { count: 5, bytes: 1.5 * 1048576 });
  assert.equal(first.size, 2);
  const last = parseAllocReport(report(["  832 (46 KB)  C_CreateCVar (c_cvars.cpp:1737) <- userinfo_t::Reset", "  9 (2.7 MB)  NETBUFFER_s::Init <- CLIENT_Construct", "  3 (40 B)  HUFFMAN_Construct <- NETWORK_Construct"]));
  assert.deepEqual(allocGrowth(first, last).map((g) => `${g.grew} ${g.count} ${g.stack}`), ["4 9 NETBUFFER_s::Init <- CLIENT_Construct", "3 3 HUFFMAN_Construct <- NETWORK_Construct"]);
  assert.equal(allocGrowth(first, last, 1).length, 1);
});

// [rc4l] With sites, allocations are recorded from the first judged cycle, reported after it and at the end, and the growth between returned.
test("a soak with sites names the stacks that grew", async () => {
  const { c, exec, ran } = fakeInstance({ verdict: { pass: false } });
  let reports = 0;
  const reporting = async (who, text) => {
    if (text.startsWith("fua_allocsites report")) return report([`  ${reports++ ? 6 : 2} (1 KB)  Leak <- Init`]);
    return exec(who, text);
  };
  const r = await runSoak(c, { cycles: 3, warmup: 1, settleMs: 0, sites: true, exec: reporting, sleep: async () => {} });
  assert.deepEqual(r.sites.map((s) => `${s.grew} ${s.stack}`), ["4 Leak <- Init"]);
  assert.ok(ran.indexOf("fua_allocsites start") > ran.indexOf("fua_soak mark"));
  assert.equal(ran.at(-1), "fua_allocsites stop");
  const short = fakeInstance({ verdict: { pass: true } });
  const s = await runSoak(short.c, { cycles: 2, warmup: 1, settleMs: 0, sites: true, exec: short.exec, sleep: async () => {} });
  assert.deepEqual(s.sites, []);
  assert.ok(!short.ran.includes("fua_allocsites start"));
});

// [rc4l] A server soak: the client connects and leaves while the server's memory is marked and judged.
test("a soak can judge another instance than the one its steps run on", async () => {
  const client = fakeInstance({ verdict: { pass: false } });
  const server = fakeInstance({ verdict: { pass: true, marks: 1 } });
  const exec = async (who, text) => (who === client.c ? client.exec(who, text) : server.exec(who, text));
  const r = await runSoak(client.c, { action: "connect 127.0.0.1:10670|disconnect", cycles: 1, settleMs: 0, judge: server.c, exec, sleep: async () => {} });
  assert.equal(r.pass, true);
  assert.deepEqual(client.ran, ["connect 127.0.0.1:10670", "disconnect"]);
  assert.deepEqual(server.ran, ["fua_soak begin", "fua_soak mark", "fua_stats json frame/ms", "fua_soak verdict 1 json"]);
});

// [rc4l] The resize audit sized the window from outside with a script beside the tool, which a soak step does now, with the text a page takes.
test("a resize step sizes the window through the caller and no console command runs for it", async () => {
  assert.deepEqual(resizeAsked("check: resize 900x700"), [900, 700]);
  assert.equal(resizeAsked("check: resize big"), null);
  assert.equal(resizeAsked("vid_setmode 900 700"), null);
  const { c, exec, ran } = fakeInstance({ verdict: { pass: true } });
  const sized = [];
  await runSoak(c, { action: "check: resize 900x700|check: resize 1280x720", cycles: 1, settleMs: 0, exec, sleep: async () => {}, resize: async (w, h) => sized.push(`${w}x${h}`) });
  assert.deepEqual(sized, ["900x700", "1280x720"]);
  assert.ok(!ran.some((t) => t.startsWith("check:")));
  await assert.rejects(runSoak(c, { action: "check: resize 900x700", cycles: 1, settleMs: 0, exec, sleep: async () => {} }), /nothing here can size a window/);
});

// [rc4l] Rapid WAD changes: a cycle reloads one set and then another, each waited on, and marks once back on the first map.
test("a cycle of several steps runs them in order and marks once", async () => {
  const { c, exec, ran } = fakeInstance({ verdict: { pass: true } });
  await runSoak(c, { action: "wad_reload a.wad map=E1M1|wad_reload b.wad map=MAP07", cycles: 1, settleMs: 0, exec, sleep: async () => {} });
  assert.deepEqual(ran.slice(0, 4), ["fua_soak begin", "wad_reload a.wad map=E1M1", "wad_reload b.wad map=MAP07", "fua_soak mark"]);
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

// [rc4l] A fight's readings swing run to run, so a gate on one stretch of it judged the stretch, not the build.
test("several readings become each counter's median p50 and p99", () => {
  const row = (name, p50, p99, available = true) => ({ name, unit: "ms", available, p50, p99, mean: p50 });
  const runs = [[row("tic/playsim", 10, 40), row("frame/ms", 5, 9)], [row("tic/playsim", 30, 70), row("frame/ms", 6, 8)], [row("tic/playsim", 12, 45), row("frame/ms", 4, 30)]];
  const m = medianReadings(runs);
  assert.deepEqual(m.map((r) => [r.name, r.p50, r.p99]), [["tic/playsim", 12, 45], ["frame/ms", 5, 9]]);
  assert.deepEqual(medianReadings([runs[0]]), runs[0]);
  assert.deepEqual(medianReadings([]), []);
  // [rc4l] A counter missing from a run, or unavailable, keeps the first reading.
  const gaps = medianReadings([[row("a", 1, 2), row("b", 1, 1, false)], [row("b", 5, 5)]]);
  assert.deepEqual(gaps.map((r) => [r.name, r.p50]), [["a", 1], ["b", 1]]);
});
