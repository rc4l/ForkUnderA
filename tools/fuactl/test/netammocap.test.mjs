import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScript, buildNetAmmoCapPk3, STRINGS, DECORATE } from "../scenarios/netammocap.mjs";
import { PCD } from "../src/acs.mjs";

test("the script sets the forbidden ammo's capacity, then adds to its counter", () => {
  const words = buildScript();
  assert.deepEqual(words.slice(0, 5), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 50, PCD.SETAMMOCAPACITY]);
  assert.equal(words[words.length - 1], PCD.TERMINATE);
});

test("the ammo derives straight from Ammo, as SetAmmoCapacity asks, and no Doom player may take it", () => {
  assert.ok(DECORATE.includes(`actor ${STRINGS[0]} : Ammo`));
  assert.ok(DECORATE.includes('Inventory.ForbiddenTo "DoomPlayer"'));
  assert.ok(buildNetAmmoCapPk3().toString("latin1").includes(`server int ${STRINGS[1]}`));
});
