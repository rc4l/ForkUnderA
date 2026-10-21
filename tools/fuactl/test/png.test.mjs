import { test } from "node:test";
import assert from "node:assert/strict";
import { pngShortUsage, PNG_USAGE } from "../src/png.mjs";

test("a png mode called with too few words gets its own usage line, a full call or an unknown mode none", () => {
  assert.equal(pngShortUsage(["--crop", "in.png", "0", "0", "1", "1"]), "usage: fuactl png --crop <in.png> <out.png> x0 y0 x1 y1 [zoom]");
  assert.equal(pngShortUsage(["--crop", "in.png", "out.png", "0", "0", "1", "1"]), null);
  assert.equal(pngShortUsage(["--crop", "in.png", "out.png", "0", "0", "1", "1", "4"]), null);
  assert.equal(pngShortUsage(["--nosuch"]), null);
  assert.equal(pngShortUsage(["a.png"]), null);
});

test("every png mode's usage names a count and a shape", () => {
  for (const [mode, [min, shape]] of Object.entries(PNG_USAGE)) {
    assert.match(mode, /^--[a-z]+$/);
    assert.ok(min >= 1 && shape.length > 0, mode);
  }
});
