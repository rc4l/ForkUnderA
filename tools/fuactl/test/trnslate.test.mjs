// [Doruk-Aksoy] The TRNSLATE template: what its wad holds, and one box and one twin per actor.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildTrnslateWad, ACTORS, BOXES, SIDES, pngPalette, jascPalette } from "../scenarios/trnslate.mjs";

test("the wad holds TRNSLATE, DECORATE, the three palette lumps and both maps", () => {
  const lumps = readWad(buildTrnslateWad());
  const names = lumps.map((l) => l.name);
  for (const n of ["MAP01", "MAP02", "TRNSLATE", "DECORATE", "ZXJASC", "ZXRAW", "ZXPNG"]) assert.ok(names.includes(n), n);
  assert.equal(lumps.find((l) => l.name === "ZXRAW").data.length, 768);
  const decorate = lumps.find((l) => l.name === "DECORATE").data.toString("latin1");
  for (const a of ACTORS) {
    assert.match(decorate, new RegExp(`actor ${a.name} \\d+`));
    const twin = a.twin ? `\\tTranslation "` : `\\t\\n`;
    assert.match(decorate, new RegExp(`actor ${a.name}Twin \\d+\\n\\{\\n\\t\\+NOGRAVITY\\n${twin}`));
  }
  assert.match(decorate, /A_SetTranslation\(""\)/);
  assert.match(decorate, /A_SetTranslation\("None"\)/);
  assert.match(decorate, /A_fua_ResetTranslation/);
});

test("the palette lumps are what the engine's reader takes", () => {
  const png = pngPalette();
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const plte = png.indexOf("PLTE");
  assert.equal(png.readUInt32BE(plte - 4), 48);
  assert.deepEqual([...png.subarray(plte + 4, plte + 7)], [255, 128, 0]);
  const jasc = jascPalette().split(/\r\n/);
  assert.deepEqual(jasc.slice(0, 3), ["JASC-PAL", "0100", "255"]);
  assert.equal(jasc.filter((l) => l === "0 255 255").length, 255);
});

test("every actor has a box inside the frame and the second side loads the twins", () => {
  assert.deepEqual(Object.keys(BOXES.MAP01), ACTORS.map((a) => a.name));
  for (const { box } of Object.values(BOXES.MAP01)) {
    assert.ok(box[0] > 0 && box[2] < 1 && box[0] < box[2], String(box));
  }
  assert.deepEqual(SIDES[1].commands, ["map MAP02"]);
});
