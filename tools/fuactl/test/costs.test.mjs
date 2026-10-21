import { test } from "node:test";
import assert from "node:assert/strict";
import { parseVisReport, parseDynStats, parseBench, costsSummary, formatCosts } from "../src/costs.mjs";

// [rc4l] The lines the engine printed at Sunder MAP14 q43 and wdiex01 on 2026-10-14, the step-0 table's sources.
const VIS = "  clipping 0.540 ms a frame over every walk, the player's in 8.0 slices, 0 clipped again for a height transfer\n" +
  "  a frame's walks reach 13046 subsectors and 44427 segs, 23307 facing the eye, 16114 past the clipper\n";
const DYN = "2D: 1 quads -> 1 draws | dynamic: 237 sprites -> 237 draws (1 submitted), 474 tris [opaque 237] | batches: 52 drawn in 1 draw calls" +
  " | runs from mirror walks: 0, dyn build 0.477 ms | world passes last frame: 2, 0.51 ms CPU: stack 0.24 main 0.27" +
  " | frame phases (ms): geometry 0.01, lights 0.00, clusters 0.01, constants 0.00, animation 0.03, draw 0.17 | layout gen 3";
const BENCH = "Diligent: 120 frames, 853752 verts (284584 tris), 47981 batches, 696 textures -- submit-only 4.2250 ms/frame, GPU 0.1789 ms/frame; presented 4.2750 ms/frame (234 fps, includes compositor wait)";

test("the walk's clipping, slices and reach come out of fua_vis_report", () => {
  assert.deepEqual(parseVisReport(VIS), { clipMs: 0.54, slices: 8, subsectors: 13046, segs: 44427 });
  assert.deepEqual(parseVisReport("nothing"), { clipMs: null, slices: null, subsectors: null, segs: null });
});

test("world passes, phases, batches and sprites come out of fua_dg_dynstats", () => {
  const d = parseDynStats(DYN);
  assert.equal(d.passes, 2);
  assert.equal(d.worldMs, 0.51);
  assert.deepEqual(d.passList, [{ name: "stack", ms: 0.24 }, { name: "main", ms: 0.27 }]);
  assert.equal(d.phases.draw, 0.17);
  assert.equal(d.phases.animation, 0.03);
  assert.equal(d.batchesDrawn, 52);
  assert.equal(d.worldDraws, 1);
  assert.equal(d.sprites, 237);
  assert.equal(d.dynBuildMs, 0.477);
  const none = parseDynStats("");
  assert.equal(none.passes, null);
  assert.deepEqual(none.passList, []);
  assert.deepEqual(none.phases, {});
  assert.equal(none.batchesDrawn, null);
  assert.equal(none.sprites, null);
});

test("the level's size, submit and GPU time come out of fua_diligent_bench", () => {
  assert.deepEqual(parseBench(BENCH), { verts: 853752, batches: 47981, textures: 696, submitMs: 4.225, gpuMs: 0.1789 });
  assert.deepEqual(parseBench("GPU n/a (no timestamp queries)"), { verts: null, batches: null, textures: null, submitMs: null, gpuMs: null });
});

test("rounds become one median p50 and p99, and the line shows what is known and marks what is not", () => {
  const s = costsSummary([{ p50: 1.2, p99: 2.2 }, { p50: 1.3, p99: 2.6 }, { p50: 1.25, p99: 2.3 }],
    parseVisReport(VIS), parseDynStats(DYN), parseBench(BENCH));
  assert.equal(s.p50, 1.25);
  assert.equal(s.p99, 2.3);
  assert.equal(s.rounds, 3);
  const text = formatCosts(s);
  assert.match(text, /^frame p50 1\.25 ms p99 2\.30 ms \| GPU 0\.18 ms \| submit 4\.22 ms \| world passes 2 at 0\.51 ms CPU \| walk clip 0\.54 ms \| batches 47981 \(52 drawn in 1 calls\) \| sprites 237 \(build 0\.48 ms\) \| verts 853752/);
  assert.match(text, /\n  passes: stack 0\.24, main 0\.27\n  phases \(ms\): geometry 0\.01/);
  const empty = costsSummary([], parseVisReport(""), parseDynStats(""), parseBench(""));
  assert.equal(empty.p50, null);
  assert.equal(formatCosts(empty), "frame p50 ? p99 ? | GPU ? | submit ? | world passes ? at ? CPU | walk clip ? | batches ? (? drawn in ? calls) | sprites ? (build ?) | verts ?");
});
