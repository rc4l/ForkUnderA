import { test } from "node:test";
import assert from "node:assert/strict";
import { overTimeSteps, stepPausedTics } from "../src/shot.mjs";

// [Doruk-Aksoy] An over-time pair waits intervalMs with the sim running by default, and steps tics in a paused one when asked.
test("an over-time pair is timed by default and stepped when asked", () => {
  assert.deepEqual(overTimeSteps(), [{ name: "0" }, { name: "1", waitMs: 600 }]);
  assert.deepEqual(overTimeSteps({ intervalMs: 250 }), [{ name: "0" }, { name: "1", waitMs: 250 }]);
  assert.deepEqual(overTimeSteps({ intervalMs: 250, stepTics: 5 }), [{ name: "0" }, { name: "1", stepTics: 5 }]);
  assert.deepEqual(overTimeSteps({ stepTics: 0 }), [{ name: "0" }, { name: "1", waitMs: 600 }]);
});

// [Doruk-Aksoy] A step listens for the refreeze before asking for it, so an engine that steps at once cannot be missed, and returns only once it arrives.
test("stepping a paused sim listens for the refreeze before asking for the step", async () => {
  const calls = [];
  let release;
  const c = {
    waitEvent: (name, timeoutMs) => { calls.push(["wait", name, timeoutMs]); return new Promise((r) => { release = r; }); },
    rpc: async (cmd, args) => { calls.push(["rpc", cmd, args]); },
  };
  let done = false;
  const stepping = stepPausedTics(c, 5, 1234).then(() => { done = true; });
  await new Promise((r) => setImmediate(r));
  assert.deepEqual(calls, [["wait", "stepped", 1234], ["rpc", "sim.step", { tics: 5 }]]);
  assert.equal(done, false, "it waits for the stepped event");
  release();
  await stepping;
  assert.equal(done, true);
});
