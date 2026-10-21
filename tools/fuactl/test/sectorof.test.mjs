import { test } from "node:test";
import assert from "node:assert/strict";
import { sectorCountFrom, paintCommands, paintOf, bisectSector, RED, GREEN } from "../src/sectorof.mjs";

// [rc4l] #28's attic: GL filled a missing texture from a neighbour that changed with the view, found by a bisect script that lived in a scratchpad.
test("the sector count is read off fua_sector_color's refusal", () => {
  assert.equal(sectorCountFrom("no sector -1: the map has 2276\n"), 2276);
  assert.equal(sectorCountFrom("Unknown command \"fua_sector_color\"\n"), null);
});

test("a range is painted in console lines of a bounded length, and an empty range is none", () => {
  assert.deepEqual(paintCommands(3, 5, RED, 2), ["fua_sector_color 3 255 0 0;fua_sector_color 4 255 0 0", "fua_sector_color 5 255 0 0"]);
  assert.deepEqual(paintCommands(0, 0, GREEN), ["fua_sector_color 0 0 255 0"]);
  assert.deepEqual(paintCommands(4, 3, RED), []);
});

test("a pixel shows a paint only when that channel alone survives", () => {
  assert.equal(paintOf([90, 0, 12]), "red");
  assert.equal(paintOf([0, 40, 0]), "green");
  assert.equal(paintOf([0, 0, 0]), null, "a black texel says nothing");
  assert.equal(paintOf([200, 180, 255]), null, "sky or a fullbright ignores the sector colour");
});

// [rc4l] A fake frame: the pixel follows sector `owner`, red when it is in lo..mid, green when it is in the rest of lo..hi.
function follows(owner, frames = []) {
  return async (lo, mid, hi) => { frames.push([lo, mid, hi]); return owner >= lo && owner <= mid ? "red" : owner <= hi ? "green" : null; };
}

test("halving finds the one sector the pixel follows, at either end and between", async () => {
  for (const owner of [0, 1, 1126, 3419, 3999]) assert.equal(await bisectSector(4000, follows(owner)), owner);
  const frames = [];
  assert.equal(await bisectSector(4000, follows(2048, frames)), 2048);
  assert.ok(frames.length <= 14, "two baselines and a dozen halvings");
  assert.equal(await bisectSector(1, follows(0)), 0);
  assert.equal(await bisectSector(0, follows(0)), null);
});

test("a pixel that ignores the paint, or stops showing it halfway, follows no sector", async () => {
  assert.equal(await bisectSector(100, async () => null), null);
  assert.equal(await bisectSector(100, async () => "red"), null, "red even when everything is green");
  let n = 0;
  assert.equal(await bisectSector(100, async (lo, mid) => (++n <= 2 ? (mid < 0 ? "green" : "red") : null)), null);
});
