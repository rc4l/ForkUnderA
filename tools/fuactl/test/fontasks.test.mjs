import { test } from "node:test";
import assert from "node:assert/strict";
import { buildFontAsksPk3, loopScript, NO_FONT, LOOP_SCRIPT, MAX_ASKS, TICS } from "../scenarios/fontasks.mjs";
import { PCD } from "../src/acs.mjs";

test("the script sets the missing font, waits a tic and jumps back to its start", () => {
  assert.deepEqual(loopScript(), [PCD.PUSHNUMBER, 0, PCD.SETFONT, PCD.DELAYDIRECT, 1, PCD.GOTO, 8]);
});

test("the map carries the script and the font's name", () => {
  const bytes = buildFontAsksPk3().toString("latin1");
  assert.ok(bytes.includes("maps/MAP01.wad") && bytes.includes(NO_FONT));
  assert.equal(LOOP_SCRIPT, 1);
  assert.equal(MAX_ASKS, 1);
  assert.ok(TICS >= 35);
});
