import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { decodePNG, px, writePNG, boxDiff } from "../src/pngcore.mjs";

// [rc4l] A w x h RGB image whose pixel colour is f(x, y), written and read back through the real codec.
function image(w, h, f) {
  const rgb = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) rgb.set(f(x, y), (y * w + x) * 3);
  const file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "pngcore-")), "img.png");
  writePNG(file, w, h, rgb);
  return decodePNG(file);
}

test("a written PNG reads back pixel for pixel", () => {
  const img = image(5, 3, (x, y) => [x * 40, y * 80, 7]);
  assert.equal(img.w, 5);
  assert.equal(img.h, 3);
  assert.deepEqual(px(img, 4, 2), [160, 160, 7]);
  assert.deepEqual(px(img, 0, 0), [0, 0, 7]);
});

test("identical frames differ by nothing", () => {
  const a = image(32, 32, (x, y) => [x * 8, y * 8, 100]);
  assert.equal(boxDiff(a, a, [0, 0, 1, 1]), 0);
});

test("a stripe one pixel out of phase is loud per pixel and quiet in blocks", () => {
  // [rc4l] The texture-phase difference two correct renderers show, which blocks are there to ignore.
  const a = image(32, 32, (x) => (x % 2 ? [255, 255, 255] : [0, 0, 0]));
  const b = image(32, 32, (x) => (x % 2 ? [0, 0, 0] : [255, 255, 255]));
  assert.equal(boxDiff(a, b, [0, 0, 1, 1], 1), 255);
  assert.equal(boxDiff(a, b, [0, 0, 1, 1], 16), 0);
});

test("a block-sized fault stays loud in blocks, and only inside the box", () => {
  // [rc4l] A wall standing where the sky should be: the whole upper half one solid colour.
  const sky = image(32, 32, () => [20, 20, 20]);
  const wall = image(32, 32, (x, y) => (y < 16 ? [220, 20, 20] : [20, 20, 20]));
  assert.ok(Math.abs(boxDiff(sky, wall, [0, 0, 1, 0.5]) - 200 / 3) < 1e-9);
  assert.equal(boxDiff(sky, wall, [0, 0.5, 1, 1]), 0);
});
