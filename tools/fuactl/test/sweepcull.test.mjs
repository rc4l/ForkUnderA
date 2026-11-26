// [Doruk-Aksoy] sweepcull's map and its column count, on synthetic pictures.
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildSweepCullWad, columnsShown, judgeSweep, RING, COLUMN_BAND, GAP_PIXELS } from "../scenarios/sweepcull.mjs";
import { readWad, parseUdmf } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";

const W = 200, H = 100;
// [Doruk-Aksoy] A dark picture with green stripes, each [x0, x1) wide, down the column band's rows unless rows are given.
function picture(stripes, rows = [Math.ceil(H * COLUMN_BAND[0]), Math.floor(H * COLUMN_BAND[1])]) {
  const data = Buffer.alloc(W * H * 3, 20);
  for (const [x0, x1] of stripes) for (let y = rows[0]; y < rows[1]; y++) for (let x = x0; x < x1; x++) data[(y * W + x) * 3 + 1] = 200;
  return { w: W, h: H, channels: 3, data };
}
const stripesAt = (n) => Array.from({ length: n }, (_, i) => [i * 20, i * 20 + 4]);

test("sweepcull's map holds the player at the centre and RING green columns on the ring", () => {
  const wad = readWad(buildSweepCullWad());
  const map = parseUdmf(wad.find((l) => l.name === "TEXTMAP").data.toString("latin1"));
  const things = map.things || map.thing;
  assert.equal(things.filter((t) => t.type === THING.TallGreenColumn).length, RING);
  assert.equal(things.filter((t) => t.type === 1 && t.x === 0 && t.y === 0).length, 1);
});

test("columnsShown counts green stripes in the band, one column's stripes split by a narrow gap as one", () => {
  assert.equal(columnsShown(picture(stripesAt(9))), 9);
  assert.equal(columnsShown(picture([[10, 14], [14 + GAP_PIXELS - 1, 20 + GAP_PIXELS]])), 1);
  assert.equal(columnsShown(picture([[10, 14], [14 + GAP_PIXELS + 2, 20 + GAP_PIXELS]])), 2);
  assert.equal(columnsShown(picture(stripesAt(5), [0, 10])), 0);
});

test("judgeSweep passes as many columns spinning as standing, and fails fewer or a still view too bare to judge", () => {
  assert.equal(judgeSweep(picture(stripesAt(9)), picture(stripesAt(9))).failed, 0);
  const fewer = judgeSweep(picture(stripesAt(9)), picture(stripesAt(3)));
  assert.equal(fewer.failed, 1);
  assert.match(fewer.lines[0], /^FAIL spinning a quarter a frame: 3 of the still view's 9 columns drawn$/);
  assert.match(judgeSweep(picture(stripesAt(2)), picture(stripesAt(2))).lines[0], /^FAIL the still view shows 2 columns/);
});
