import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeChange, CHANGES, buildNetSettingTextPk3 } from "../scenarios/netsettingtext.mjs";

const INF = CHANGES[0];

test("a change passes when its line names the wanted value", () => {
  assert.deepEqual(judgeChange("sv_coop_damagefactor changed to: infinity\n", INF), []);
  assert.deepEqual(judgeChange("sv_coop_damagefactor changed to: 1.#J\n", INF), ['setting sv_coop_damagefactor to inf printed "1.#J", not infinity']);
  assert.deepEqual(judgeChange("", INF), ["setting sv_coop_damagefactor to inf printed no change ()"]);
});

test("the changes end on each setting's default, and the pk3 holds the map", () => {
  assert.deepEqual(CHANGES.filter((c) => c.cvar === "sv_coop_damagefactor").at(-1).set, "1");
  assert.deepEqual(CHANGES.filter((c) => c.cvar === "sv_gravity").at(-1).set, "800");
  assert.ok(buildNetSettingTextPk3().toString("latin1").includes("maps/MAP01.wad"));
});
