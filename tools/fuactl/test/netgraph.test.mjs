import { test } from "node:test";
import assert from "node:assert/strict";
import { lineColorRows, judgeLineColors, LINE_COLORS, CONFIGS } from "../scenarios/netgraph.mjs";

// [rc4l] A 600 by 400 grey frame with the given coloured runs, each [colour, x0, x1, y].
const frame = (runs) => (x, y) => {
  for (const [c, x0, x1, ry] of runs) if (y === ry && x >= x0 && x < x1) return c;
  return [90, 90, 90];
};

test("only the region's coloured pixels count, by line and by row, the stat text's rows left out", () => {
  const found = lineColorRows(frame([[LINE_COLORS.In, 10, 26, 300], [LINE_COLORS.In, 30, 40, 310], [LINE_COLORS.Out, 10, 26, 302], [LINE_COLORS.Out, 500, 560, 350], [LINE_COLORS.In, 10, 30, 10], [LINE_COLORS.Out, 5, 300, 380]]), 600, 400);
  assert.deepEqual(found, { In: { count: 26, rows: 2 }, Out: { count: 16, rows: 1 } });
});

test("a graph needs both lines and an In line off the bottom", () => {
  assert.deepEqual(judgeLineColors({ In: { count: 80, rows: 12 }, Out: { count: 40, rows: 4 } }), []);
  assert.deepEqual(judgeLineColors({ In: { count: 0, rows: 0 }, Out: { count: 0, rows: 0 } }), ["no In line or legend (0 pixels)", "no Out line or legend (0 pixels)"]);
  assert.deepEqual(judgeLineColors({ In: { count: 30, rows: 2 }, Out: { count: 20, rows: 1 } }), ["the In line lies on 2 row(s), never off the bottom"]);
});

test("the client shows the graph without the status bar", () => {
  assert.equal(CONFIGS[0].clientCvars.cl_netgraph, 1);
  assert.equal(CONFIGS[0].clientCvars.screenblocks, 12);
});
