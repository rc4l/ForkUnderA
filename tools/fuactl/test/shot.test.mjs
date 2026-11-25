import { test } from "node:test";
import assert from "node:assert/strict";
import { overTimeSteps, stepPausedTics, BUILD_TARGETS, PK3_TARGETS, repeatedCaptures, repeatNote } from "../src/shot.mjs";

// [MGOOOOOO] A burst marks each capture equal to the one before, and a failed capture repeats nothing.
test("a burst finds the captures that repeat the one before", () => {
  const a = Buffer.from("frame a"), b = Buffer.from("frame b");
  assert.deepEqual(repeatedCaptures([a, b, Buffer.from("frame b"), b, a]), [2, 3]);
  assert.deepEqual(repeatedCaptures([a, null, null, a]), []);
  assert.deepEqual(repeatedCaptures([a]), []);
  assert.deepEqual(repeatedCaptures([]), []);
});

// [MGOOOOOO] The note names the causes seen so far, and the slow Vulkan HUD capture only for a Vulkan burst.
test("a burst's repeat note names the causes to rule out", () => {
  assert.equal(repeatNote([]), null);
  assert.match(repeatNote([1]), /^capture 1 repeats the one before byte for byte: a still scene does that, .*fua_render_in_background 0/);
  assert.match(repeatNote([1, 2]), /^captures 1, 2 repeat the one before byte for byte: /);
  assert.match(repeatNote([3], { side: "vk" }), /Vulkan capture also shows a HUD animation slower .*fuactl window --shot gl/);
  assert.doesNotMatch(repeatNote([3], { side: "gl" }), /Vulkan/);
  assert.doesNotMatch(repeatNote([3]), /NOT RESPONDING|fuactl hang/);
});

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

test("a build compiles the engine, its data pk3s and the server registry", () => {
  for (const t of ["zdoom", ...PK3_TARGETS, "forkundera-server-registry"]) assert.ok(BUILD_TARGETS.includes(t), t);
});
