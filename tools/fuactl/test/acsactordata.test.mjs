import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeActorData, buildAcsActorDataPk3, FN, MAX_KEYS } from "../scenarios/acsactordata.mjs";

const WARN = 'fua_SetActorData: a ExplosiveBarrel already carries 64 keys, so "k64" was not set.';
const FRIEND = 2, REMOVED = 2, SPAWNED = (1 << 20) | 2;
const good = {
  d1: `${WARN}\nd1 1 3 3 1 -9 0 1 4 1 ${FRIEND} ${FRIEND} 0 0 ${MAX_KEYS} 0 1 9 5 0`,
  d2: `d2 -1 ${REMOVED} ${SPAWNED} 0`,
  d3: "d3 4 0 0 0",
};

test("the run passes with every step's answer and names each wrong one", () => {
  assert.deepEqual(judgeActorData(good), []);
  assert.match(judgeActorData({ ...good, d1: good.d1.replace(WARN, "") }).join(), /warned 0 times/);
  assert.match(judgeActorData({ ...good, d1: `${WARN}\n${WARN}\n${good.d1.split("\n")[1]}` }).join(), /warned 2 times/);
  assert.match(judgeActorData({ ...good, d2: `d2 -1 ${REMOVED} ${SPAWNED} 1` }).join(), /has its values: 1/);
  assert.match(judgeActorData({ ...good, d3: "d3 0 0 0 0" }).join(), /after a savegame the value read 0/);
  assert.match(judgeActorData({ ...good, d3: "" }).join(), /script d3 logged nothing/);
});

test("the store's functions keep their numbers and the pk3 holds the map", () => {
  assert.deepEqual([FN.SetActorData, FN.GetActorData, FN.HasActorData, FN.ClearActorData], [24032, 24033, 24034, 24035]);
  assert.ok(buildAcsActorDataPk3().toString("latin1").includes("maps/MAP01.wad"));
});
