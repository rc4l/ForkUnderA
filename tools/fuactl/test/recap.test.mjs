import { test } from "node:test";
import assert from "node:assert/strict";
import { captureSeconds, formatRecap, parseCaptureCsv, slowFrameRhythm, slowSecondsGrew } from "../src/recap.mjs";

// [rc4l] A 50 ms frame every 500 ms was the heap walk behind a gauge; slow frames at a steady beat say a timer, not the game.
test("slow frames at a steady beat are told apart from scattered ones", () => {
  const frames = (pattern) => pattern.map((ms) => ({ "frame/ms": ms }));
  const beat = frames(Array.from({ length: 2000 }, (_, i) => (i % 100 === 99 ? 50 : 4.5)));
  const r = slowFrameRhythm(beat);
  assert.equal(r.count, 20);
  assert.ok(Math.abs(r.medianGapMs - 495.5) < 1, String(r.medianGapMs));
  assert.equal(r.steady, true);
  const scattered = slowFrameRhythm(frames([4, 30, 4, 4, 4, 4, 4, 4, 30, 30, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 30]));
  assert.equal(scattered.count, 4);
  assert.equal(scattered.steady, false);
  assert.deepEqual(slowFrameRhythm(frames([4, 4])), { count: 0, medianGapMs: 0, steady: false });
  const lines = formatRecap(captureSeconds(beat), []);
  assert.ok(lines.some((l) => /^\d+ frames over 20 ms, 49\d ms apart at the median, at a steady beat/.test(l)), lines.join("\n"));
});

// [rc4l] A capture of n frames each frameMs long with a GPU time, one tic a frame threaded.
function capture(n, frameMs, gpuMs) {
  const head = "frame,time_ms,frame/ms,frame/tics,handoff/threaded_tics,handoff/held_frames,gpu/frame_ms,mem/private_bytes,gpu/mem/total_bytes";
  const rows = Array.from({ length: n }, (_, i) => `${i},${i * frameMs},${frameMs},1,1,0,${gpuMs},${1000 + i},${gpuMs * 1000}`);
  return [head, ...rows].join("\n");
}

test("a capture reads back as a row of numbers a frame, keyed by its header", () => {
  const rows = parseCaptureCsv("frame,frame/ms\n1,4.5\n2,x\n");
  assert.deepEqual(rows, [{ frame: 1, "frame/ms": 4.5 }, { frame: 2, "frame/ms": 0 }]);
  assert.deepEqual(parseCaptureCsv(""), []);
});

test("frames are cut into whole seconds by their own frame times, a part second left out", () => {
  const seconds = captureSeconds(parseCaptureCsv(capture(250, 10, 2)));
  assert.equal(seconds.length, 2);
  assert.deepEqual([seconds[0].fps, seconds[0].tics, seconds[0].threaded, seconds[0].held], [100, 100, 100, 0]);
  assert.deepEqual([seconds[0].p50, seconds[0].p99, seconds[0].max], [10, 10, 10]);
});

// [rc4l] The slow quarter's GPU time stands out against the fast quarter's; memory is left out, and so is anything that shrank.
test("the slow seconds' growth is named against the fast ones, biggest first", () => {
  const fast = parseCaptureCsv(capture(300, 5, 2));
  const slow = parseCaptureCsv(capture(100, 20, 15));
  const seconds = captureSeconds([...fast, ...slow]);
  const grew = slowSecondsGrew(seconds);
  assert.equal(grew[0][0], "frame/ms");
  assert.deepEqual(grew.map((g) => g[0]).sort(), ["frame/ms", "gpu/frame_ms"]);
  assert.deepEqual(grew.find((g) => g[0] === "gpu/frame_ms").slice(1), [2, 15]);
  const chosen = slowSecondsGrew(seconds, { fast: seconds.slice(0, 1), slow: seconds.slice(-1), limit: 1 });
  assert.equal(chosen.length, 1);
  const lines = formatRecap(seconds, grew);
  assert.match(lines[0], /^sec\s+fps\s+tics\s+thread\s+held/);
  assert.match(lines[1], /^\s+0\s+200\s+200\s+200\s+0\s+5\.00/);
  assert.ok(lines.some((l) => /^gpu\/frame_ms\s+2\.000\s+15\.000$/.test(l)));
});
