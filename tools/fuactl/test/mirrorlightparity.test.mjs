import test from "node:test";
import assert from "node:assert/strict";
import { COLOURS } from "../uzdoom/flipparity.mjs";
import { MAPS, buildMirrorLightPk3, readColours, readLit } from "../uzdoom/mirrorlightparity.mjs";

// [Doruk-Aksoy] A w by h image of `bg` with each box [x0, y0, x1, y1, rgb] filled.
function image(w, h, bg, boxes) {
  const data = Buffer.alloc(w * h * 4);
  for (let i = 0; i < w * h; i++) data.set([...bg, 255], i * 4);
  for (const [x0, y0, x1, y1, c] of boxes) for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) data.set([...c, 255], (y * w + x) * 4);
  return { w, h, data, channels: 4 };
}

test("readColours names each sprite and leaves a narrow run out", () => {
  const img = image(300, 100, [128, 128, 128], [[20, 30, 60, 70, COLOURS.R], [100, 40, 108, 60, COLOURS.O], [150, 30, 190, 70, COLOURS.B]]);
  assert.equal(readColours(img), "RB");
});

test("readLit tells a red-lit grey sprite from a plain one in a black room", () => {
  const img = image(300, 100, [0, 0, 0], [[20, 30, 60, 70, [160, 60, 60]], [100, 30, 140, 70, [90, 90, 90]]]);
  assert.deepEqual(readLit(img), ["lit", "unlit"]);
});

test("the pk3 holds three maps and the flags", () => {
  const text = buildMirrorLightPk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "maps/MAP03.wad", "+INVISIBLEINMIRRORS", "+ONLYVISIBLEINMIRRORS", "+NODYNAMICLIGHTING"]) assert.ok(text.includes(name), name);
  assert.equal(MAPS.length, 3);
});
