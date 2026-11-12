import { test } from "node:test";
import assert from "node:assert/strict";
import { buildDecorate, buildKeyconf, judgeClass, className, CLASS_COUNT, CHOSEN } from "../scenarios/netclasses.mjs";

test("more classes than a byte numbers, the chosen one a different class once cut to a byte", () => {
  assert.ok(CLASS_COUNT > 256 && CHOSEN < CLASS_COUNT && CHOSEN > 255);
  assert.ok((CHOSEN & 0xff) < CLASS_COUNT && (CHOSEN & 0xff) !== CHOSEN);
});

test("every class is declared and added, after the stock ones are cleared", () => {
  assert.equal(buildDecorate().split("\n").filter(Boolean).length, CLASS_COUNT);
  assert.ok(buildDecorate().includes(`actor ${className(CHOSEN)} : DoomPlayer { Player.DisplayName "${className(CHOSEN)}" }`));
  const lines = buildKeyconf().split("\n").filter(Boolean);
  assert.equal(lines[0], "clearplayerclasses");
  assert.equal(lines.length, CLASS_COUNT + 1);
  assert.equal(lines[CHOSEN + 1], `addplayerclass ${className(CHOSEN)}`);
});

test("only the chosen class passes for the chooser's body", () => {
  assert.deepEqual(judgeClass([{ player: 1, c: className(CHOSEN) }], 1), []);
  assert.deepEqual(judgeClass([{ player: 1, c: className(CHOSEN & 0xff) }], 1), [`spawned player 1 as ${className(CHOSEN & 0xff)}, not ${className(CHOSEN)}`]);
  assert.deepEqual(judgeClass([{ player: 0, c: className(CHOSEN) }], 1), ["holds no body for player 1"]);
  assert.deepEqual(judgeClass(undefined, 1), ["holds no body for player 1"]);
});
