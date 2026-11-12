import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeStartItems, DECORATE, KEYCONF, PLAYER_CLASS, REFUSED } from "../scenarios/startitemrefused.mjs";

test("the start passes as the class, with its pistol and without the refused item", () => {
  assert.deepEqual(judgeStartItems({ bodyClass: PLAYER_CLASS, inventory: "Pistol #3 (1/1)\nFist #2 (1/1)\n" }), []);
  assert.deepEqual(judgeStartItems({ bodyClass: "DoomPlayer", inventory: `${REFUSED} #1 (1/1)` }), [
    `the player is a DoomPlayer, not a ${PLAYER_CLASS}`, "the player started without its pistol", `the player holds the ${REFUSED} that refuses it`]);
});

test("the item refuses the class absolutely and is one of its start items, which KEYCONF makes the only class", () => {
  assert.ok(DECORATE.includes(`Inventory.ForbiddenTo "${PLAYER_CLASS}"`) && DECORATE.includes("+INVENTORY.RESTRICTABSOLUTELY"));
  assert.ok(DECORATE.includes(`Player.StartItem "${REFUSED}"`));
  assert.equal(KEYCONF, `clearplayerclasses\naddplayerclass ${PLAYER_CLASS}\n`);
});
