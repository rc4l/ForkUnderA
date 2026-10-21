import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { decodePNG, decodePNGBytes, px, writePNG, encodePNG, cropImage, boxDiff, boxDiffMax, blockDiffs, abDiff, samplePoints, isBlankImage, boxMean, boxStats, filesAndBox, boxArgs } from "../src/pngcore.mjs";

// [rc4l] A w x h RGB image whose pixel colour is f(x, y), written and read back through the real codec.
function image(w, h, f) {
  const rgb = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) rgb.set(f(x, y), (y * w + x) * 3);
  const file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "pngcore-")), "img.png");
  writePNG(file, w, h, rgb);
  return decodePNG(file);
}

test("a PNG in memory decodes as its file would", () => {
  const rgb = Buffer.from([10, 20, 30, 200, 100, 0]);
  const img = decodePNGBytes(encodePNG(2, 1, rgb));
  assert.deepEqual([img.w, img.h, px(img, 0, 0), px(img, 1, 0)], [2, 1, [10, 20, 30], [200, 100, 0]]);
});

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

test("a box's mean is the brightness of the pixels it covers, and pixel coordinates are refused", () => {
  // [rc4l] 4x2 RGB: the left half black, the right half (30, 60, 90), whose (r+g+b)/3 is 60.
  const data = Buffer.alloc(4 * 2 * 3);
  for (const [x, y] of [[2, 0], [3, 0], [2, 1], [3, 1]]) data.set([30, 60, 90], (y * 4 + x) * 3);
  const img = { w: 4, h: 2, channels: 3, data };
  assert.equal(boxMean(img, [0, 0, 1, 1]), 30);
  assert.equal(boxMean(img, [0.5, 0, 1, 1]), 60);
  assert.equal(boxMean(img, [0, 0, 0.5, 1]), 0);
  assert.throws(() => boxMean(img, [0, 0, 640, 1]), /fractions/);
});

test("box stats give the brightness range and each channel's mean, so a cast shows where brightness hides it", () => {
  const img = image(4, 2, (x) => (x < 2 ? [30, 0, 0] : [0, 0, 90]));
  const t = boxStats(img, [0, 0, 1, 1]);
  assert.deepEqual([t.lo, t.hi, t.mean, t.r, t.g, t.b], [10, 30, 20, 15, 0, 45]);
  assert.equal(boxStats(img, [0, 0, 0.5, 1]).b, 0);
});

test("a box follows the files only when the last four arguments are numbers", () => {
  assert.deepEqual(filesAndBox(["a.png", "0", "0.1", "1", "0.5"]), { files: ["a.png"], box: [0, 0.1, 1, 0.5] });
  assert.deepEqual(filesAndBox(["a.png", "b.png"]), { files: ["a.png", "b.png"], box: [0, 0, 1, 1] });
  assert.deepEqual(filesAndBox(["a.png", "b.png", "c.png", "d.png", "e.png"], [0.3, 0.5, 0.7, 0.8]).box, [0.3, 0.5, 0.7, 0.8]);
  assert.deepEqual(filesAndBox(["0", "0", "1", "1"]).files, ["0", "0", "1", "1"]);   // [rc4l] four numbers and no file is not a box
  assert.deepEqual(filesAndBox(["a.png", "b.png", "0,0.5,1,0.9"]), { files: ["a.png", "b.png"], box: [0, 0.5, 1, 0.9] });
  assert.deepEqual(filesAndBox(["a.png", "b.png", "0,0.5,,0.9"]).files.length, 3);   // [rc4l] a comma list with a gap is not a box
  assert.deepEqual(filesAndBox(["0,0,1,1"]).files, ["0,0,1,1"]);
});

// [rc4l] `png --boxdiff a b 0.05,0.72,0.65,0.9` read the comma box as four separate numbers and refused it as NaN.
test("a box reads as one comma list or as four numbers, with what follows it", () => {
  assert.deepEqual(boxArgs(["0.05,0.72,0.65,0.9", "8"]), { box: [0.05, 0.72, 0.65, 0.9], rest: ["8"] });
  assert.deepEqual(boxArgs(["0", "0.5", "1", "1", "4"]), { box: [0, 0.5, 1, 1], rest: ["4"] });
  assert.deepEqual(boxArgs(["0", "0", "1", "1"]).rest, []);
});

test("a fault in one block is the largest block difference however quiet it is in the mean", () => {
  const a = image(64, 64, () => [100, 100, 100]);
  const b = image(64, 64, (x, y) => (x < 16 && y < 16 ? [160, 100, 100] : [100, 100, 100]));
  assert.equal(boxDiffMax(a, b, [0, 0, 1, 1], 16), 20);
  assert.equal(boxDiff(a, b, [0, 0, 1, 1], 16), 20 / 16);
  assert.equal(blockDiffs(a, b, [0, 0, 1, 1], 16).length, 16);
  assert.equal(boxDiffMax(a, a, [0, 0, 1, 1]), 0);
});
