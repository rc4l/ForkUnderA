import test from "node:test";
import assert from "node:assert/strict";
import { ACTORS, COLOURS, buildFlipPk3, colourOf, spriteSpans, quarters, readSprites, judgeFlips } from "../uzdoom/flipparity.mjs";

// [Doruk-Aksoy] A w by h image of grey with each box [x0, y0, x1, y1, [tl, tr, bl, br]] filled by quarter.
function image(w, h, boxes) {
  const data = Buffer.alloc(w * h * 4, 128);
  for (const [x0, y0, x1, y1, q] of boxes)
    for (let y = y0; y < y1; y++)
      for (let x = x0; x < x1; x++) {
        const c = COLOURS[q[(y < (y0 + y1) / 2 ? 0 : 2) + (x < (x0 + x1) / 2 ? 0 : 1)]];
        data.set([...c, 255], (y * w + x) * 4);
      }
  return { w, h, data, channels: 4 };
}

test("colourOf names a picture colour and leaves the grey room out", () => {
  assert.equal(colourOf([250, 10, 5]), "R");
  assert.equal(colourOf([240, 120, 10]), "O");
  assert.equal(colourOf([128, 128, 128]), null);
});

test("quarters reads a sprite's four corners and a span list finds each sprite", () => {
  const img = image(200, 100, [[20, 30, 60, 70, "RGBY"], [100, 30, 140, 70, "GRYB"]]);
  const spans = spriteSpans(img);
  assert.deepEqual(spans, [[20, 60], [100, 140]]);
  assert.equal(quarters(img, spans[0]), "RGBY");
  assert.equal(quarters(img, spans[1]), "GRYB");
});

test("readSprites wants one sprite per actor", () => {
  const one = readSprites(image(200, 100, [[20, 30, 60, 70, "RGBY"]]));
  assert.match(one.fault, /1 sprites, not 7/);
  const boxes = ACTORS.map((_, i) => [10 + i * 40, 30, 30 + i * 40, 70, "RGBY"]);
  assert.deepEqual(readSprites(image(300, 100, boxes)).quarters, ACTORS.map(() => "RGBY"));
});

test("judgeFlips names each actor that reads unlike UZDoom", () => {
  const uz = ACTORS.map(() => "RGBY");
  assert.deepEqual(judgeFlips(uz, uz), []);
  const ours = [...uz];
  ours[1] = "GRYB";
  assert.deepEqual(judgeFlips(uz, ours), ["ZxQuadX reads GRYB, UZDoom RGBY"]);
});

test("the pk3 holds the map, the actors' flags and both sprites", () => {
  const text = buildFlipPk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "decorate.txt", "sprites/ZXQDA0.png", "sprites/ZXRTA8.png", "+SPRITEFLIP", "+YFLIP"]) assert.ok(text.includes(name), name);
});
