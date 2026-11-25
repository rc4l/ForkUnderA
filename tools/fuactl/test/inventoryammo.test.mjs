import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildInventoryAmmoWad, clips, judgeInventoryAmmo } from "../scenarios/inventoryammo.mjs";

test("the wad holds the map, a gun spending 2 clips a shot and an item that fires with useammo", () => {
  const lumps = readWad(buildInventoryAmmoWad());
  assert.deepEqual(lumps.map((l) => l.name).slice(0, 2), ["MAP01", "TEXTMAP"]);
  const decorate = lumps.find((l) => l.name === "DECORATE").data.toString("latin1");
  assert.match(decorate, /Weapon\.AmmoType "Clip"\s+Weapon\.AmmoUse 2/);
  assert.match(decorate, /Pickup:\s+TNT1 A 0 A_FireCustomMissile\("ZXAmmoBall", 0, 1, 0, 0\)/);
});

test("clips reads the Clip amount from printinv, and -1 when there is none", () => {
  assert.equal(clips("Pistol #3 (1/1)\nClip #4 (48/200)\nZXAmmoGun #6 (1/1)\n"), 48);
  assert.equal(clips("Pistol #3 (1/1)\n"), -1);
  assert.equal(clips(undefined), -1);
});

test("the verdict passes only a shot that spent the gun's 2 clips", () => {
  assert.match(judgeInventoryAmmo("MAP01 vk", { before: -1, after: -1 }).line, /nothing was tested/);
  assert.deepEqual(judgeInventoryAmmo("MAP01 vk", { before: 50, after: 50 }),
    { line: "FAIL MAP01 vk: the item's shot spent no ammo (50 clips before and after)", failed: 1 });
  assert.match(judgeInventoryAmmo("MAP01 vk", { before: 50, after: 49 }).line, /spent 1 clip\(s\), not the gun's 2/);
  assert.deepEqual(judgeInventoryAmmo("MAP01 vk", { before: 50, after: 48 }),
    { line: "PASS MAP01 vk: the item's shot spent the gun's 2 clips, 50 then 48", failed: 0 });
});
