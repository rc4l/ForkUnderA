import { test } from "node:test";
import assert from "node:assert/strict";
import { judge, scenarioOutcome, isMain } from "../src/template.mjs";
import { pathToFileURL } from "node:url";

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

test("a scenario's own sides are compared by name", () => {
  const frames = { far: { flat: flat(32, 32, [40, 40, 40]), clustered: flat(32, 32, [0, 0, 0]) } };
  const r = judge("MAP01", frames, BOXES, [32, 32], ["flat", "clustered"]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^FAIL MAP01 far/);
});

test("a scenario exits 0 only when nothing failed, and 3 when it refused to run", () => {
  assert.deepEqual(scenarioOutcome("x", { lines: ["PASS a"], failed: 0 }), { lines: ["PASS a", "all x boxes under their ceilings"], code: 0 });
  assert.equal(scenarioOutcome("x", { lines: [], failed: 2 }).code, 1);
  assert.equal(scenarioOutcome("x", { lines: [], failed: 1, refused: true }).code, 3);
});

test("a scenario counts as main only when node was started on that very file", () => {
  const url = pathToFileURL("/repo/scenarios/a.mjs").href;
  assert.equal(isMain(url, "/repo/scenarios/a.mjs"), true);
  assert.equal(isMain(url, "/repo/test/a.test.mjs"), false);
  assert.equal(isMain(url, undefined), false);
});
