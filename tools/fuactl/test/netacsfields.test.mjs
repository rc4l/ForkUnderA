import { test } from "node:test";
import assert from "node:assert/strict";
import { CHECKS } from "../scenarios/acsfields.mjs";
import { STEPS, LATE, judgeMachine, buildNetFieldsPk3 } from "../scenarios/netacsfields.mjs";

const f1 = `f1 ${CHECKS.map((c) => c.want).join(" ")}`;

test("the clients write their copies first, the server reads its own before it writes, then runs the checks", () => {
  assert.deepEqual(STEPS.map((s) => [s.script, s.clients !== false, s.server !== false]), [[1, true, false], [2, false, true], [1, false, true]]);
  assert.deepEqual(LATE.map((s) => s.script), [1]);
});

test("every machine passes acsfields' checks, and the server fails when a client's write reached it", () => {
  assert.deepEqual(judgeMachine({ f1 }, { role: "observer" }), []);
  assert.deepEqual(judgeMachine({ f1, f2: "f2 0 0" }, { role: "server" }), []);
  assert.match(judgeMachine({ f1, f2: "f2 12 5" }, { role: "server" }).join(), /read 12 5, not 0 0/);
  assert.match(judgeMachine({ f1 }, { role: "server" }).join(), /read nothing/);
  assert.match(judgeMachine({ f1: "" }, { role: "player" }).join(), /logged nothing/);
});

test("the pk3 builds with the map and the barrels' DECORATE", () => {
  const text = buildNetFieldsPk3().toString("latin1");
  assert.ok(text.includes("maps/MAP01.wad") && text.includes("decorate.txt"));
});
