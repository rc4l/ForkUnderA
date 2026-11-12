import { test } from "node:test";
import assert from "node:assert/strict";
import { stepUnevenness, judgeEvenness, CONFIGS, UNEVEN_FLOOR } from "../scenarios/netmovebuffer.mjs";

// [rc4l] Samples of player 0 moving up y by each step in turn, one a tic from tic 100, with player 1 standing still beside.
const run = (steps) => {
  let y = 0;
  return [0, ...steps].map((d, i) => { y += d; return { tic: 100 + i, actors: [{ player: 1, x: 0, y: 0 }, { player: 0, x: 0, y }] }; });
};

test("steady steps are even and a missed tic leaves its step out", () => {
  assert.deepEqual(stepUnevenness(run([15, 15, 15, 15]), 0), { steps: 4, median: 15, uneven: 0, share: 0 });
  const gap = run([15, 15, 15]).filter((s) => s.tic !== 102);
  assert.equal(stepUnevenness(gap, 0).steps, 1);
});

test("a stalled tic and the double step after it are both uneven", () => {
  assert.deepEqual(stepUnevenness(run([15, 0, 30, 15, 15, 15]), 0), { steps: 6, median: 15, uneven: 2, share: 2 / 6 });
  assert.deepEqual(stepUnevenness([], 0), { steps: 0, median: 0, uneven: 0, share: 0 });
});

const view = (uneven, steps = 100, median = 15) => ({ steps, median, uneven, share: uneven / steps });

test("the buffered view must be at most half as uneven as an unbuffered view uneven enough to tell", () => {
  assert.deepEqual(judgeEvenness(view(6), view(21)), []);
  assert.deepEqual(judgeEvenness(view(12), view(21)), ["with the buffers 12 of 100 steps were uneven, against 21 of 100 without"]);
  assert.deepEqual(judgeEvenness(view(2), view(Math.floor(UNEVEN_FLOOR * 100))), ["without the buffers only 15 of 100 steps were uneven, so the check cannot tell"]);
});

test("too few steps or a runner standing still say nothing", () => {
  assert.deepEqual(judgeEvenness(view(0, 30), view(20)), ["only 30 buffered steps a tic apart were sampled"]);
  assert.deepEqual(judgeEvenness(view(0, 100, 1), view(20)), ["the runner barely moved buffered (median step 1.00)"]);
  assert.deepEqual(judgeEvenness(view(0), view(20, 10)), ["only 10 unbuffered steps a tic apart were sampled"]);
});

test("the observer alone has the jittered link", () => {
  assert.match(CONFIGS[0].jitter, /^0,\d+$/);
  assert.equal(CONFIGS[0].observers, 1);
});
