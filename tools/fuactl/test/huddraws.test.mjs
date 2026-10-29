// [Doruk-Aksoy] The HUD draws template: what its status bar draws in each order, its fonts and images, and which maps run the HudMessages.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { readWad, readPk3 } from "../src/udmf.mjs";
import { buildHudDrawsPk3, SBARINFO, SIDES, BOXES, TC, TC_STRINGS, TC_GLYPHS, TG_GLYPHS, GOLD } from "../scenarios/huddraws.mjs";

const files = () => Object.fromEntries(readPk3(buildHudDrawsPk3()).map((f) => [f.name, f.data]));

test("the status bar draws its panel at the right edge before the text, number and translucent image unless ZXHudOracle puts it last", () => {
  const block = SBARINFO.slice(SBARINFO.indexOf("InInventory ZXHudOracle"), SBARINFO.indexOf("InInventory ZXFontOracle"));
  const [held, unheld] = block.split("else");
  const order = (s) => ["ZXPANEL", "drawstring", "drawnumber", "alpha 0.5"].map((k) => s.indexOf(k));
  const [panelLast, ...drawnFirst] = order(held);
  assert.ok(drawnFirst.every((i) => i >= 0 && i < panelLast), "held: the panel comes after every draw");
  const [panelFirst, ...drawnAfter] = order(unheld);
  assert.ok(drawnAfter.every((i) => i > panelFirst), "unheld: the panel comes before every draw");
  assert.match(SBARINFO, /drawimage "ZXPANEL", -256, 0;/);
  assert.doesNotMatch(SBARINFO, /drawstring [^"]*"\$/, "a string starting with $ is a LANGUAGE lookup");
  assert.equal((SBARINFO.match(/statusbar (fullscreen|normal), fullscreenoffsets/g) ?? []).length, 2);
});

test("each truecolor string's glyph images sit where drawstring puts its glyphs, one pixel in and a glyph apart", () => {
  TC_STRINGS.forEach((s, i) => {
    assert.ok(SBARINFO.includes(`drawstring ${s.font}, ${s.color}, "0123", 40, ${TC.y[i]}, 0, alignment(left);`), s.color);
    for (let k = 0; k < 4; k++) assert.ok(SBARINFO.includes(`drawimage "${s.images}${k}", ${41 + 16 * k}, ${TC.y[i]};`), `${s.color} ${k}`);
  });
});

test("the fonts, images and oracles are in the pk3, and MAP01 and MAP03 run the HudMessages", () => {
  const f = files();
  const fontdefs = f["fontdefs.txt"].toString("latin1");
  assert.match(fontdefs, /ZXFONT\n\{\n  0 ZXNUM0\n[\s\S]*  \$ ZXNUMD\n\}/);
  assert.match(fontdefs, /ZXTCFONT\n\{\n  0 ZXTC0\n  1 ZXTC1\n  2 ZXTC2\n  3 ZXTC3\n\}/);
  assert.match(fontdefs, /ZXTCFIX\n\{\n  DONTTRANSLATE\n  0 ZXTC0\n/);
  for (const n of ["ZXPANEL", "ZXSQUARE", "ZXNUM0", "ZXNUM9", "ZXNUMD", "ZXTC0", "ZXTC3", "ZXTG0", "ZXTG3", "ZXTCROW", "ZXTGROW"]) assert.ok(f[`graphics/${n}.png`], n);
  assert.match(f["decorate.txt"].toString("latin1"), /Actor ZXHudOracle : Inventory[\s\S]*Actor ZXFontOracle : Inventory/);
  assert.deepEqual(readWad(f["maps/MAP01.wad"]).map((l) => l.name), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  assert.deepEqual(readWad(f["maps/MAP02.wad"]).map((l) => l.name), ["MAP02", "TEXTMAP", "ENDMAP"]);
  assert.deepEqual(readWad(f["maps/MAP03.wad"]).map((l) => l.name), ["MAP03", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
});

test("the truecolor glyphs have more shades than a palette keeps, and Gold is textcolors.txt's", () => {
  const shades = new Set();
  for (const g of TC_GLYPHS) for (let i = 0; i < g.length; i += 3) shades.add(g.readUIntBE(i, 3));
  assert.ok(shades.size > 256, `${shades.size} shades`);
  assert.ok(TG_GLYPHS.every((g, k) => !g.equals(TC_GLYPHS[k])));
  const textcolors = readFileSync(new URL("../../../src/zandronum/wadsrc/static/textcolors.txt", import.meta.url), "latin1");
  const [, from, to] = textcolors.match(/^Gold\s*\{\s*#(\w{6})\s+#(\w{6})\s*\n/m);
  const rgb = (hex) => [0, 2, 4].map((i) => parseInt(hex.slice(i, i + 2), 16));
  assert.deepEqual(GOLD, [{ start: 0, end: 256, from: rgb(from), to: rgb(to) }]);
});

test("every box compares two sides the scenario launches", () => {
  const names = new Set(SIDES.map((s) => s.name));
  for (const [n, b] of Object.entries(BOXES.MAP01)) assert.ok(b.pair.every((s) => names.has(s)), n);
  assert.deepEqual(SIDES.find((s) => s.name === "oracle").commands, ["give ZXHudOracle"]);
  assert.deepEqual(SIDES.find((s) => s.name === "fontoracle").commands, ["map MAP03"]);
});
