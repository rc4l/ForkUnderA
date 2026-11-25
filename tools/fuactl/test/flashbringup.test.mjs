import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildFlashBringUpWad, flashTics, judgeFlashBringUp } from "../scenarios/flashbringup.mjs";

test("the wad holds the map and a gun that raises in zero-tic frames into a Ready that starts its flash", () => {
  const lumps = readWad(buildFlashBringUpWad());
  assert.deepEqual(lumps.map((l) => l.name).slice(0, 2), ["MAP01", "TEXTMAP"]);
  const decorate = lumps.find((l) => l.name === "DECORATE").data.toString("latin1");
  assert.match(decorate, /Select:\s+PISG A{16,} 0 A_Raise/);
  assert.match(decorate, /Ready:\s+PISG A 0 A_GunFlash/);
  assert.match(decorate, /Flash:\s+TNT1 A 0 A_GiveInventory\("ZXFlashTic", 1\)\s+TNT1 A 1\s+Loop/);
});

test("flashTics reads ZXFlashTic's amount from printinv, and 0 when the flash never gave one", () => {
  assert.equal(flashTics("Pistol #3 (1/1)\nZXFlashTic #7 (54/1000000)\nZXFlashGun #6 (1/1)\n"), 54);
  assert.equal(flashTics("Pistol #3 (1/1)\n"), 0);
  assert.equal(flashTics(undefined), 0);
});

test("the verdict passes only a flash that started and kept counting", () => {
  assert.equal(judgeFlashBringUp("MAP01 vk", { early: 0, late: 0 }).failed, 1);
  assert.match(judgeFlashBringUp("MAP01 vk", { early: 0, late: 0 }).line, /never started/);
  assert.deepEqual(judgeFlashBringUp("MAP01 vk", { early: 1, late: 1 }),
    { line: "FAIL MAP01 vk: the flash ran 1 tic(s) and stopped as the gun came up", failed: 1 });
  assert.deepEqual(judgeFlashBringUp("MAP01 vk", { early: 54, late: 89 }),
    { line: "PASS MAP01 vk: the flash kept running, 54 tics and then 89", failed: 0 });
});
