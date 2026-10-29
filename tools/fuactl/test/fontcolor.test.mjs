// [Doruk-Aksoy] fontcolor.mjs against the values features/truecolorfont's unit tests pin, so the scenario's expected glyphs are the engine's own.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { luminance, rangeColor, luminosityRamp, luminosityRange, luminosityTable, recolor } from "../src/fontcolor.mjs";

const A = [{ start: 0, end: 127, from: [0, 0, 0], to: [100, 0, 0] }, { start: 128, end: 256, from: [100, 0, 0], to: [255, 255, 255] }];
const B = [{ start: 0, end: 256, from: [0, 0, 0], to: [0, 0, 200] }];
const testRamp = () => Array.from({ length: 256 }, (_, i) => [100 + Math.trunc(i / 4), i, 255 - i]);

test("brightness weighs the channels as the engine does", () => {
  assert.deepEqual([[0, 0, 0], [255, 255, 255], [255, 0, 0], [0, 255, 0], [0, 0, 255]].map((c) => luminance(...c)), [0, 255, 76, 142, 36]);
});

test("a level takes the step that holds it, or the last step clamped", () => {
  assert.deepEqual([0, 127, 128, 192, 256, 300].map((v) => rangeColor(A, v)),
    [[0, 0, 0], [100, 0, 0], [100, 0, 0], [177, 127, 127], [255, 255, 255], [255, 255, 255]]);
  assert.deepEqual(rangeColor(B, -10), [0, 0, 0]);
  const ramp = luminosityRamp(B);
  assert.deepEqual([ramp[0], ramp[128], ramp[255]], [[0, 0, 0], [0, 0, 100], [0, 0, 199]]);
});

test("the range spans every image's texels", () => {
  assert.deepEqual(luminosityRange([Buffer.from([255, 0, 0]), Buffer.from([0, 255, 0, 0, 0, 0])]), { min: 0, max: 142 });
});

test("the table stretches the range over the ramp and scales texels outside it", () => {
  const ramp = testRamp();
  const t = luminosityTable(ramp, 64, 191);
  assert.deepEqual([t[64], t[128], t[191]], [ramp[0], ramp[127], ramp[253]]);
  assert.deepEqual([t[32], t[255]], [[50, 0, 127], [217, 255, 0]]);
  const black = luminosityTable(ramp, 0, 0);
  assert.deepEqual([black[0], black[10]], [ramp[0], ramp[255]]);
  assert.deepEqual(luminosityTable(ramp, 255, 0), luminosityTable(ramp, 0, 255));
});

test("recolouring takes each texel's brightness entry", () => {
  const table = Array.from({ length: 256 }, (_, i) => [i, 0, 255 - i]);
  assert.deepEqual([...recolor(Buffer.from([255, 255, 255, 0, 0, 0, 255, 0, 0]), table)], [255, 0, 0, 0, 0, 255, 76, 0, 179]);
});
