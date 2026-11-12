import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeAngles, buildResetAnglePk3, ANGLES, STATUE, STATUE_ED, SIDES } from "../scenarios/resetangle.mjs";

const statue = (x, angle, health = 20) => ({ c: STATUE, x, angle, health });

test("one statue between the compass points and one on them", () => {
  assert.ok(ANGLES.some((a) => a % 45 !== 0) && ANGLES.some((a) => a % 45 === 0));
});

test("the map holds a statue per angle, numbered by the DECORATE ednum", () => {
  const text = buildResetAnglePk3().toString("latin1");
  assert.ok(text.includes(`actor ${STATUE} : ZombieMan ${STATUE_ED}`));
  for (const a of ANGLES) assert.ok(text.includes(`angle = ${a};`), `angle ${a}`);
});

test("statues are judged west to east against the map's angles, a full turn the same angle", () => {
  assert.deepEqual(judgeAngles([statue(576, 135.0001), statue(192, 30.2)]), []);
  assert.deepEqual(judgeAngles([statue(192, 390), statue(576, 135)]), []);
  assert.deepEqual(judgeAngles([statue(192, 0), statue(576, 135)]), ["the statue spawned at 30 faces 0.0"]);
});

test("a dead or missing statue fails, and the launch has monsters in survival", () => {
  assert.deepEqual(judgeAngles([statue(192, 30), statue(576, 135, 0)]), ["1 living statues, not 2"]);
  assert.deepEqual(judgeAngles(undefined), ["0 living statues, not 2"]);
  assert.deepEqual(SIDES[0].launchCvars, { survival: 1, sv_nomonsters: 0 });
});
