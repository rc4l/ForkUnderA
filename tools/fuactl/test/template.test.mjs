import { test } from "node:test";
import assert from "node:assert/strict";
import { judge, scenarioOutcome, isMain, sideCommands, boxWithin, runScenarioIfMain, scenarioOptions, scenarioRun, templateScenarioFiles, probeChoices } from "../src/template.mjs";
import { pathToFileURL } from "node:url";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

// [rc4l] A decoded-frame stand-in: w x h, every pixel the same RGB.
function flat(w, h, rgb) {
  const data = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) data.set(rgb, i * 3);
  return { w, h, channels: 3, colorType: 2, data };
}

const BOXES = { sky: { box: [0, 0, 1, 0.5], ceiling: 6 } };

test("a box under its ceiling passes and one over it fails", () => {
  const frames = {
    same: { gl: flat(32, 32, [10, 10, 10]), vk: flat(32, 32, [12, 12, 12]) },
    wall: { gl: flat(32, 32, [10, 10, 10]), vk: flat(32, 32, [200, 10, 10]) },
  };
  const r = judge("MAP01", frames, BOXES, [32, 32]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^PASS MAP01 same/);
  assert.match(r.lines[1], /^FAIL MAP01 wall/);
});

test("a missing capture is a failure, never a pass", () => {
  const r = judge("MAP01", { gone: { gl: flat(32, 32, [0, 0, 0]), vk: null } }, BOXES, [32, 32]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /capture failed/);
});

test("frames of the wrong size fail rather than compare the wrong pixels", () => {
  const r = judge("MAP01", { small: { gl: flat(16, 16, [0, 0, 0]), vk: flat(16, 16, [0, 0, 0]) } }, BOXES, [32, 32]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /boxes assume 32x32/);
});

test("a box that names its vantage is judged only at that vantage", () => {
  const frames = { step: { gl: flat(32, 32, [0, 0, 0]), vk: flat(32, 32, [90, 0, 0]) },
    lintel: { gl: flat(32, 32, [0, 0, 0]), vk: flat(32, 32, [0, 0, 0]) } };
  const r = judge("MAP01", frames, { gap: { box: [0, 0, 1, 1], ceiling: 1, vantage: "lintel" } }, [32, 32]);
  assert.equal(r.failed, 0);
  assert.equal(r.lines.length, 1);
  assert.match(r.lines[0], /^PASS MAP01 lintel/);
});

test("a scenario imported rather than run keeps its options for probing", () => {
  const url = "file:///scenarios/imported.mjs";
  runScenarioIfMain(url, () => ({ name: "imported" }));
  assert.equal(scenarioOptions(url)().name, "imported");
  assert.equal(scenarioOptions("file:///scenarios/never.mjs"), null);
});

test("an imported scenario runs as one call through the comparison it named", async () => {
  const url = "file:///scenarios/runnable.mjs";
  const seen = [];
  runScenarioIfMain(url, () => ({ name: "runnable", port: 7 }), async (opts) => { seen.push(opts.port); return { lines: ["PASS a"], failed: 0 }; });
  const out = await scenarioRun(url)();
  assert.deepEqual(seen, [7]);
  assert.deepEqual(out, { lines: ["PASS a", "all runnable boxes within their limits"], code: 0 });
  assert.equal(scenarioRun("file:///scenarios/never.mjs"), null);
});

test("only files that register a template scenario are run, all of them or those named", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-scen-"));
  fs.writeFileSync(path.join(dir, "a.mjs"), "runScenarioIfMain(import.meta.url, () => ({}));");
  fs.writeFileSync(path.join(dir, "b.mjs"), "runScenarioIfMain(import.meta.url, () => ({}));");
  fs.writeFileSync(path.join(dir, "measure.mjs"), "await launchSomething();");
  fs.writeFileSync(path.join(dir, "bench.json"), "{}");
  try {
    assert.deepEqual(templateScenarioFiles(dir).sort(), ["a.mjs", "b.mjs"]);
    assert.deepEqual(templateScenarioFiles(dir, ["b", "measure"]), ["b.mjs"]);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("a probe takes the named map, vantage and side, else the first map, vantage and Vulkan side", () => {
  const opts = { maps: ["MAP01", "MAP02"], vantages: [{ name: "a" }, { name: "b" }],
    sides: [{ name: "gl", gl: true }, { name: "vk" }] };
  const plain = probeChoices(opts);
  assert.deepEqual([plain.map, plain.vantage.name, plain.side.name], ["MAP01", "a", "vk"]);
  const named = probeChoices(opts, { map: "MAP02", vantage: "b", side: "gl" });
  assert.deepEqual([named.map, named.vantage.name, named.side.name], ["MAP02", "b", "gl"]);
  assert.equal(probeChoices({ maps: ["MAP01"], vantages: [{ name: "a" }], sides: [{ name: "only", gl: true }] }).side.name, "only");
  assert.throws(() => probeChoices(opts, { vantage: "z" }), /no vantage z/);
  assert.throws(() => probeChoices(opts, { side: "z" }), /no side z/);
  assert.throws(() => probeChoices(opts, { map: "MAP09" }), /no map MAP09/);
});

test("a scenario's own sides are compared by name", () => {
  const frames = { far: { flat: flat(32, 32, [40, 40, 40]), clustered: flat(32, 32, [0, 0, 0]) } };
  const r = judge("MAP01", frames, BOXES, [32, 32], ["flat", "clustered"]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^FAIL MAP01 far/);
});

test("a scenario exits 0 only when nothing failed, and 3 when it refused to run", () => {
  assert.deepEqual(scenarioOutcome("x", { lines: ["PASS a"], failed: 0 }), { lines: ["PASS a", "all x boxes within their limits"], code: 0 });
  assert.equal(scenarioOutcome("x", { lines: [], failed: 2 }).code, 1);
  assert.equal(scenarioOutcome("x", { lines: [], failed: 1, refused: true }).code, 3);
});

test("a scenario counts as main only when node was started on that very file", () => {
  const url = pathToFileURL("/repo/scenarios/a.mjs").href;
  assert.equal(isMain(url, "/repo/scenarios/a.mjs"), true);
  assert.equal(isMain(url, "/repo/test/a.test.mjs"), false);
  assert.equal(isMain(url, undefined), false);
});

test("a side sets its cvars first and then runs its commands, in order", () => {
  assert.deepEqual(sideCommands({ cvars: { fua_surface_bands: 0, gl_lights: 1 }, commands: ["fua_mapbake_now"] }),
    ["fua_surface_bands 0", "gl_lights 1", "fua_mapbake_now"]);
  assert.deepEqual(sideCommands({ name: "gl", gl: true }), []);
});

test("a box with a floor must change by at least that much, and one with a ceiling by no more", () => {
  assert.equal(boxWithin(0.0, { floor: 1 }), false);
  assert.equal(boxWithin(2.4, { floor: 1 }), true);
  assert.equal(boxWithin(0.0, { ceiling: 0.05 }), true);
  assert.equal(boxWithin(0.3, { ceiling: 0.05 }), false);
  const frames = { pool: { before: flat(32, 32, [10, 10, 10]), after: flat(32, 32, [10, 10, 10]) } };
  const r = judge("MAP01", frames, { water: { box: [0, 0, 1, 1], floor: 1 } }, [32, 32], ["before", "after"]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^FAIL MAP01 pool .* floor 1$/);
});

test("a box can be judged per pixel, where block means would hide a ripple", () => {
  const stripes = (phase) => ({ w: 32, h: 32, channels: 3, colorType: 2,
    data: Buffer.from(Array.from({ length: 32 * 32 }, (_, i) => ((i + phase) % 2 ? [200, 200, 200] : [0, 0, 0])).flat()) });
  const frames = { pool: { before: stripes(0), after: stripes(1) } };
  assert.equal(judge("MAP01", frames, { water: { box: [0, 0, 1, 1], floor: 1 } }, [32, 32], ["before", "after"]).failed, 1);
  assert.equal(judge("MAP01", frames, { water: { box: [0, 0, 1, 1], floor: 1, block: 1 } }, [32, 32], ["before", "after"]).failed, 0);
});
