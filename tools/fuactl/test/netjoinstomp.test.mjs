// [Doruk-Aksoy] The join-stomp scenario's map and verdicts: every start under a target, what a join stomped, and a run that stomped nothing failing.
import { test } from "node:test";
import assert from "node:assert/strict";
import { STARTS, TARGET, TARGET_NUM, TARGET_DECORATE, buildJoinStompWad, stomped, stompVerdict, CONFIGS } from "../scenarios/netjoinstomp.mjs";

test("every player start, 1 to 8, holds a target, the starts far enough apart that a spawn stomps only its own", () => {
  assert.deepEqual(STARTS.map((s) => s.type), [1, 2, 3, 4, 4001, 4002, 4003, 4004]);
  for (const a of STARTS) for (const b of STARTS) if (a !== b) assert.ok(Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y)) >= 2 * 64 + 2 * 16, `${a.type} and ${b.type}`);
  assert.match(TARGET_DECORATE, new RegExp(`actor ${TARGET} : Arachnotron ${TARGET_NUM}`));
  assert.match(TARGET_DECORATE, /Speed 0/);
  assert.doesNotMatch(TARGET_DECORATE, /A_NoBlocking|A_Fall/, "its corpse stays solid, so its start stays blocked");
  const text = buildJoinStompWad().toString("latin1");
  assert.equal(text.split(`type = ${TARGET_NUM};`).length - 1, STARTS.length);
  assert.ok(text.includes("DECORATE"));
});

test("a join stomped the targets alive before it and dead after, nothing else", () => {
  const t = (netid, health, c = TARGET) => ({ c, netid, health });
  const found = stomped([t(1, 500), t(2, 500), t(3, -999500), t(4, 100, "DoomPlayer")], [t(1, -999500), t(2, 500), t(3, -999500), t(4, -999900, "DoomPlayer"), t(5, 0)]);
  assert.deepEqual(found.map((a) => a.netid), [1]);
});

test("the late joins must have stomped a target, or agreement proves nothing", () => {
  assert.deepEqual(stompVerdict("MAP01 lan", 2), { line: "PASS MAP01 lan: the late joins stomped 2 target(s) on the server", failed: 0 });
  assert.deepEqual(stompVerdict("MAP01 lan", 0), { line: "FAIL MAP01 lan: no late join stomped a target on the server", failed: 1 });
  assert.deepEqual(CONFIGS.map((c) => c.name), ["lan", "ping100"]);
});
