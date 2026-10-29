// [Doruk-Aksoy] The HUD draws template: what its status bar draws in each order, its font and images, and which map runs the HudMessages.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad, readPk3 } from "../src/udmf.mjs";
import { buildHudDrawsPk3, SBARINFO, SIDES, BOXES } from "../scenarios/huddraws.mjs";

const files = () => Object.fromEntries(readPk3(buildHudDrawsPk3()).map((f) => [f.name, f.data]));

test("the status bar draws its panel at the right edge before the text, number and translucent image unless ZXHudOracle puts it last", () => {
  const [held, unheld] = SBARINFO.split("else");
  const order = (s) => ["ZXPANEL", "drawstring", "drawnumber", "alpha 0.5"].map((k) => s.indexOf(k));
  const [panelLast, ...drawnFirst] = order(held);
  assert.ok(drawnFirst.every((i) => i >= 0 && i < panelLast), "held: the panel comes after every draw");
  const [panelFirst, ...drawnAfter] = order(unheld);
  assert.ok(drawnAfter.every((i) => i > panelFirst), "unheld: the panel comes before every draw");
  assert.match(SBARINFO, /drawimage "ZXPANEL", -256, 0;/);
  assert.doesNotMatch(SBARINFO, /drawstring [^"]*"\$/, "a string starting with $ is a LANGUAGE lookup");
  assert.equal((SBARINFO.match(/statusbar (fullscreen|normal), fullscreenoffsets/g) ?? []).length, 2);
});

test("the font, panel and square are in the pk3, and only MAP01 runs the HudMessages", () => {
  const f = files();
  assert.match(f["fontdefs.txt"].toString("latin1"), /ZXFONT\n\{\n  0 ZXNUM0\n[\s\S]*  \$ ZXNUMD\n\}/);
  for (const n of ["ZXPANEL", "ZXSQUARE", "ZXNUM0", "ZXNUM9", "ZXNUMD"]) assert.ok(f[`graphics/${n}.png`], n);
  assert.match(f["decorate.txt"].toString("latin1"), /Actor ZXHudOracle : Inventory/);
  assert.deepEqual(readWad(f["maps/MAP01.wad"]).map((l) => l.name), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  assert.deepEqual(readWad(f["maps/MAP02.wad"]).map((l) => l.name), ["MAP02", "TEXTMAP", "ENDMAP"]);
});

test("every box compares two sides the scenario launches", () => {
  const names = new Set(SIDES.map((s) => s.name));
  for (const [n, b] of Object.entries(BOXES.MAP01)) assert.ok(b.pair.every((s) => names.has(s)), n);
  assert.deepEqual(SIDES.find((s) => s.name === "oracle").commands, ["give ZXHudOracle"]);
});
