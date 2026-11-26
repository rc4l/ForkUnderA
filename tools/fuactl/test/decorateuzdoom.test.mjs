// [Doruk-Aksoy] The DECORATE-gap scenario: one check actor and two markers per check, and the judge's reading of a world.
import { test } from "node:test";
import assert from "node:assert/strict";
import { CHECKS, LIVE, LIVE_SPOT, liveFor, checkClass, checksFor, buildDecorate, buildMap, checkSpot, judgeWorld, judgeLive, actorDef, compareOfflineAndOnline, judgeUzdoomLog, runInUzdoom, NEW_FLAGS, checkDecorate, ROOM_SIZE } from "../scenarios/decorateuzdoom.mjs";

test("every check has its actor, both markers and a doomednum the map places", () => {
  const decorate = buildDecorate();
  const map = buildMap();
  for (const c of CHECKS) {
    assert.match(decorate, new RegExp(`actor ${checkClass(c)} \\d+\\n`), c.id);
    assert.match(decorate, new RegExp(`actor ZxPass_${c.id}\\n`), c.id);
    assert.match(decorate, new RegExp(`actor ZxFail_${c.id}\\n`), c.id);
    // [Doruk-Aksoy] Something spawns each marker the judge looks for, and no label is the keyword Fail.
    assert.match(decorate, new RegExp(`A_SpawnItemEx\\("ZxPass_${c.id}"`), c.id);
    assert.match(decorate, new RegExp(`A_SpawnItemEx\\("ZxFail_${c.id}"`), c.id);
    assert.doesNotMatch(decorate, /^\t(Fail|Pass):$/m);
    const num = Number(new RegExp(`actor ${checkClass(c)} (\\d+)`).exec(decorate)[1]);
    assert.ok(map.things.some((t) => t.type === num), `${c.id} is placed`);
  }
});

test("check ids are unique and every spot is inside the room", () => {
  assert.equal(new Set(CHECKS.map((c) => c.id)).size, CHECKS.length);
  CHECKS.forEach((_, i) => {
    const { x, y } = checkSpot(i);
    assert.ok(x > 0 && x < ROOM_SIZE && y > 0 && y < ROOM_SIZE, `${x},${y}`);
  });
});

test("UZDoom's build leaves out what only ours has", () => {
  const fua = CHECKS.filter((c) => c.fua);
  assert.ok(fua.length > 0);
  const decorate = buildDecorate({ withFua: false });
  for (const c of fua) assert.doesNotMatch(decorate, new RegExp(checkClass(c)));
  assert.doesNotMatch(decorate, /A_fua_/);
  assert.equal(checksFor(false).length, CHECKS.length - fua.length);
});

test("actorDef writes a line a property and a state, labels a tab in", () => {
  assert.equal(actorDef("A", ["+SOLID"], ["TNT1 A 1", "Next:", "Stop"], 5),
    "actor A 5\n{\n\t+SOLID\n\tStates\n\t{\n\tSpawn:\n\t\tTNT1 A 1\n\tNext:\n\t\tStop\n\t}\n}\n");
});

// [Doruk-Aksoy] A world where every check passed, with the rows the translation and scale checks read.
function passingWorld() {
  const actors = CHECKS.map((c) => ({ c: `ZxPass_${c.id}` }));
  actors.push({ c: "ZxChk_transnone", tr: 0, sx: 1, sy: 1 });
  actors.push({ c: "ZxChk_transreset", tr: 0x10007, sx: 1, sy: 1 });
  actors.push({ c: "ZxTransTwin", tr: 0x10007 });
  actors.push({ c: "ZxChk_setscale", tr: 0, sx: 2, sy: 0 });
  return actors;
}

test("a translation or scale check with no row, or a row without the field, is a fault", () => {
  const noRow = passingWorld().filter((a) => a.c !== "ZxChk_setscale");
  assert.ok(judgeWorld(noRow).some((f) => /^setscale: no row/.test(f)));
  const noField = passingWorld();
  delete noField.find((a) => a.c === "ZxChk_setscale").sy;
  delete noField.find((a) => a.c === "ZxChk_transreset").tr;
  const faults = judgeWorld(noField);
  assert.ok(faults.some((f) => /^setscale: scale 2xundefined/.test(f)));
  assert.ok(faults.some((f) => /^transreset: translation undefined/.test(f)));
});

test("a world where every check passed has no fault", () => {
  assert.deepEqual(judgeWorld(passingWorld()), []);
});

test("a fail marker, a missing marker, and wrong rows are each named", () => {
  const world = passingWorld().filter((a) => a.c !== "ZxPass_clamp" && a.c !== "ZxPass_minmax");
  world.push({ c: "ZxFail_clamp" });
  world.find((a) => a.c === "ZxChk_transnone").tr = 0x10007;
  world.find((a) => a.c === "ZxChk_transreset").tr = 0;
  world.find((a) => a.c === "ZxChk_setscale").sy = 2;
  const faults = judgeWorld(world);
  assert.ok(faults.some((f) => /^clamp .*failed/.test(f)));
  assert.ok(faults.some((f) => /^minmax .*left no marker/.test(f)));
  assert.ok(faults.some((f) => /^transnone: translation/.test(f)));
  assert.ok(faults.some((f) => /^transreset: translation 0, not the class's/.test(f)));
  assert.ok(faults.some((f) => /^setscale: scale 2x2/.test(f)));
});

test("the class-translation check without its twin is a fault, and UZDoom's judge skips it", () => {
  const world = passingWorld().filter((a) => a.c !== "ZxTransTwin");
  assert.ok(judgeWorld(world).some((f) => /transreset: no twin/.test(f)));
  assert.ok(!judgeWorld(world, { withFua: false }).some((f) => /transreset/.test(f)));
});

test("offline and online results are joined", async () => {
  const r = await compareOfflineAndOnline({}, { offline: async () => ({ lines: ["a"], failed: 1 }), online: async () => ({ lines: ["b", "c"], failed: 0 }) });
  assert.deepEqual(r, { lines: ["a", "b", "c"], failed: 1 });
});

test("the live actors are spawned by script 1 at a spot of their own, and UZDoom's build leaves out ours", () => {
  const decorate = buildDecorate();
  for (const l of LIVE) assert.match(decorate, new RegExp(`actor ${l.cls}\n`));
  assert.ok(buildMap().things.some((t) => t.id === LIVE_SPOT));
  assert.equal(liveFor(false).length, LIVE.filter((l) => !l.fua).length);
  assert.doesNotMatch(buildDecorate({ withFua: false }), /ZxLiveReset/);
});

test("the live judge wants each change held and a client's rows like the server's", () => {
  const good = [{ c: "ZxLiveNone", tr: 0, sx: 1, sy: 1 }, { c: "ZxLiveReset", tr: 0x30007, sx: 1, sy: 1 }, { c: "ZxLiveScale", tr: 0, sx: 2, sy: 0 }];
  assert.deepEqual(judgeLive(good), []);
  assert.deepEqual(judgeLive(good, good), []);
  const stale = good.map((r) => (r.c === "ZxLiveNone" ? { ...r, tr: 0x30007 } : r));
  assert.ok(judgeLive(stale, good).some((f) => /ZxLiveNone: translation 196615, scale 1x1, not translation 0/.test(f)));
  assert.ok(judgeLive(stale, good).some((f) => /ZxLiveNone: unlike the server's/.test(f)));
  assert.ok(judgeLive(good.slice(1)).some((f) => /ZxLiveNone: not held/.test(f)));
  assert.ok(judgeLive(good, good.slice(0, 2)).some((f) => /ZxLiveScale: unlike the server's/.test(f)));
});

test("UZDoom's log judge wants a pass line for every shared check", () => {
  const shared = checksFor(false);
  const all = shared.map((c) => `zxcheck ${c.id} pass`).join("\n");
  assert.deepEqual(judgeUzdoomLog(all), []);
  const one = shared[0];
  assert.deepEqual(judgeUzdoomLog(all.replace(`zxcheck ${one.id} pass`, `zxcheck ${one.id} fail`)), [`${one.id} (${one.what}) failed`]);
  assert.deepEqual(judgeUzdoomLog(all.replace(`zxcheck ${one.id} pass`, "")), [`${one.id} (${one.what}) logged nothing`]);
  assert.equal(judgeUzdoomLog("").length, shared.length);
});

test("the UZDoom run hands it the pk3 without our checks and judges its log", async () => {
  const fs = await import("node:fs"), os = await import("node:os"), path = await import("node:path");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "duz-test-"));
  let asked = null;
  const pass = runInUzdoom({ dir, run: (o) => { asked = o; return checksFor(false).map((c) => `zxcheck ${c.id} pass`).join("\n"); } });
  assert.equal(pass.failed, 0);
  assert.match(pass.line, /^PASS uzdoom: \d+ shared checks pass in UZDoom$/);
  assert.equal(asked.map, "MAP01");
  assert.ok(fs.existsSync(asked.pk3));
  assert.match(asked.chain, /quit$/);
  const fail = runInUzdoom({ dir, run: () => "" });
  assert.equal(fail.failed, 1);
  assert.match(fail.line, /^FAIL uzdoom: /);
  fs.rmSync(dir, { recursive: true, force: true });
});

test("the new-flags check sets and reads every one of UZDoom's gameplay flags", () => {
  const c = CHECKS.find((x) => x.id === "newflags");
  const d = checkDecorate(c);
  for (const f of NEW_FLAGS) {
    assert.ok(d.includes(`\t+${f}\n`), `+${f}`);
    assert.ok(d.includes(`A_CheckFlag("${f}", `), `A_CheckFlag ${f}`);
  }
  assert.equal(new Set(NEW_FLAGS).size, NEW_FLAGS.length);
});

test("a check judged by another actor has no Passed label of its own, and that actor logs the verdict", () => {
  for (const c of CHECKS.filter((x) => x.verdictBy)) {
    assert.ok(!checkDecorate(c).includes("\tPassed:"), c.id);
    assert.ok((c.defs || "").includes(`zxcheck ${c.id} pass`), c.id);
  }
});
