import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeHeights, buildNetPickupHeightPk3, buildScript, DECORATE, PICKUP, STEP, AT } from "../scenarios/netpickupheight.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

const at = (server, client) => ({ server, client });

test("the heights pass when both put the pickup on the step before and after", () => {
  const health = 101, highest = STEP;
  assert.deepEqual(judgeHeights({ before: at(STEP, STEP), after: at(STEP, STEP), health, highest }), []);
  assert.deepEqual(judgeHeights({ before: at(STEP, STEP), after: at(STEP, STEP), health, highest: 2 * STEP }), [`the client respawned the pickup as high as ${2 * STEP} before it fell to the step at ${STEP}`]);
  assert.deepEqual(judgeHeights({ before: at(STEP, STEP), after: at(STEP, 2 * STEP), health }), [`the client respawned the pickup at ${2 * STEP}, the server at ${STEP}`]);
  assert.deepEqual(judgeHeights({ before: at(STEP, STEP), after: at(null, null), health }), [`the server respawned the pickup at null, not ${STEP}`]);
  assert.deepEqual(judgeHeights({ before: at(0, STEP), after: at(STEP, STEP), health }), [`before it was taken the pickup stood at 0 on the server and ${STEP} on the client, not ${STEP}`]);
  assert.deepEqual(judgeHeights({ before: at(STEP, STEP), after: at(STEP, STEP), health: 100 }), ["the player never took the pickup, health 100"]);
});

test("the pickup comes back a second after it is taken, and the pk3 holds it", () => {
  assert.match(DECORATE, /RespawnTics 35/);
  assert.match(DECORATE, /\+INVENTORY\.ALWAYSRESPAWN/);
  assert.match(DECORATE, /A_ChangeFlag\("DROPPED", 0\)/);
  assert.deepEqual(buildScript().slice(0, 12), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, AT.x * 65536, PCD.PUSHNUMBER, AT.y * 65536, PCD.PUSHNUMBER, STEP * 65536, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0]);
  assert.ok(buildScript().includes(ACSF.SpawnForced));
  const bytes = buildNetPickupHeightPk3().toString("latin1");
  assert.ok(bytes.includes("decorate.txt") && bytes.includes(`heightfloor = ${STEP};`) && DECORATE.includes(PICKUP));
});
