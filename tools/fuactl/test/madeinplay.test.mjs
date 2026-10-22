import { test } from "node:test";
import assert from "node:assert/strict";
import { buildMadeInPlayWad, judgeMadeInPlay, STEPS } from "../scenarios/madeinplay.mjs";

test("the made-in-play map builds and every step names its action", () => {
  assert.ok(buildMadeInPlayWad().length > 0);
  assert.ok(STEPS.every((s) => s.action && s.commands.length > 0));
});

test("steps that make no shader or pipeline pass", () => {
  assert.equal(judgeMadeInPlay("x", [{ action: "floor marks", shaders: 0, pipelines: 0 }]).failed, 0);
});

test("a step that compiles in play fails, named with what it made", () => {
  const r = judgeMadeInPlay("x", [{ action: "floor marks", shaders: 2, pipelines: 2 }, { action: "menu", shaders: 0, pipelines: 0 }, { action: "automap", shaders: 0, pipelines: 2 }]);
  assert.equal(r.failed, 1);
  assert.equal(r.line, "FAIL x: made in play by floor marks (2 shaders, 2 pipelines), automap (0 shaders, 2 pipelines)");
});

test("a binary without the counters fails rather than passing on nothing", () => {
  assert.match(judgeMadeInPlay("x", []).line, /no gpu\/made/);
  assert.match(judgeMadeInPlay("x", [{ action: "menu", shaders: NaN, pipelines: 0 }]).line, /no gpu\/made/);
});
