import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { decodePNG, px, writePNG, encodePNG, cropImage, boxDiff, abDiff, samplePoints, isBlankImage } from "../src/pngcore.mjs";

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

test("encoded bytes are the PNG writePNG puts on disk", () => {
  const rgb = Buffer.from([255, 255, 255, 0, 0, 0]);
  const file = path.join(os.tmpdir(), "fua-encode-test.png");
  writePNG(file, 2, 1, rgb);
  assert.deepEqual(encodePNG(2, 1, rgb), fs.readFileSync(file));
  assert.equal(encodePNG(2, 1, rgb).subarray(1, 4).toString("latin1"), "PNG");
});

// [rc4l] A crop at zoom 0.6 used to write a 614.4-pixel width into the PNG header, which nothing could open.
test("a crop shrinks to whole pixels and keeps its corners", () => {
  const img = { w: 10, h: 10, channels: 3, colorType: 2, data: Buffer.alloc(300) };
  img.data[0] = 200;
  const half = cropImage(img, [0, 0, 1, 1], 0.6);
  assert.deepEqual([half.w, half.h], [6, 6]);
  assert.equal(half.rgb[0], 200);
  const big = cropImage(img, [0, 0, 0.5, 0.5], 2, 1);
  assert.deepEqual([big.w, big.h, big.srcW], [10, 10, 5]);
});

test("identical frames differ by nothing", () => {
  const a = image(32, 32, (x, y) => [x * 8, y * 8, 100]);
  assert.equal(boxDiff(a, a, [0, 0, 1, 1]), 0);
});

test("a box given in pixels is refused rather than walked", () => {
  const a = image(32, 32, () => [0, 0, 0]);
  assert.throws(() => boxDiff(a, a, [0, 0, 1280, 720]), /fractions of the frame/);
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

test("points are sampled in the order asked", () => {
  const img = image(4, 4, (x, y) => [x, y, 9]);
  assert.deepEqual(samplePoints(img, [[3, 1], [0, 2]]), [[3, 1, 9], [0, 2, 9]]);
});

// [rc4l] GL's first readback after a launch came back black three times this session; shot retakes it.
test("a black capture is blank and one lit pixel in four is not", () => {
  assert.equal(isBlankImage(image(16, 16, () => [0, 0, 1])), true);
  assert.equal(isBlankImage(image(16, 16, (x, y) => (x === 8 && y === 8 ? [40, 0, 0] : [0, 0, 0]))), false);
});

test("a frozen A/B reports the switch's change beside the repeat's noise", () => {
  const a = image(32, 32, () => [10, 10, 10]);
  const b = image(32, 32, () => [40, 40, 40]);
  assert.deepEqual(abDiff(a, b, a), { changed: 30, noise: 0 });
});
