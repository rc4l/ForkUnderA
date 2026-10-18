import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { viewpoint, SANDBOX, HOLD_TICS, ticOf, sleep, SLOW_COMMAND, launchModeOf, glFrameOf, glFrameFor, echoedCvar, rendererReport, RENDERER_PROBE } from "../src/capture.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
// This tool's own directory -- NOT tools/, which holds unrelated CI tripwires that drive nothing.
const fuactlDir = path.resolve(here, "..");

test("the viewing camera stands back ALONG the firing direction and looks down at the target", () => {
  // Facing east (yaw 0): the camera must end up west of the target, above it, pitched down.
  const v = viewpoint({ x: 100, y: 0, z: 50 }, 0, { back: 100, up: 100 });

  assert.ok(Math.abs(v.x - 0) < 1e-6, "camera should be back along -x");
  assert.ok(Math.abs(v.y - 0) < 1e-6);
  assert.equal(v.z, 150);
  assert.ok(v.pitch > 0, "positive pitch looks DOWN; a negative one photographs the ceiling");
  assert.ok(Math.abs(v.pitch - 45) < 1e-6);
});

test("closing in keeps the angle and only shortens the distance", () => {
  // The retry when a camera lands inside a wall must not swing the view somewhere else, or the
  // retry frames a different thing than the shot it is standing in for.
  const full = viewpoint({ x: 0, y: 0, z: 0 }, 37, { back: 200, up: 100, frac: 1 });
  const near = viewpoint({ x: 0, y: 0, z: 0 }, 37, { back: 200, up: 100, frac: 0.4 });

  assert.ok(Math.abs(full.pitch - near.pitch) < 1e-9, "pitch must not change when closing in");
  assert.equal(near.yaw, full.yaw);
  const dist = (v) => Math.hypot(v.x, v.y, v.z);
  assert.ok(dist(near) < dist(full));
});

test("the sandbox holds still, aims freely, and does not set freelook to the bit that disables it", () => {
  // [rc4l] sv_freelook is a MASK cvar over DF_NO_FREELOOK|DF_YES_FREELOOK. `sv_freelook 1` sets the
  // NO bit -- the engine then centres the view every tic, pitch is silently discarded, and every
  // shot aimed at a floor or ceiling join is fired level. This asserts the value, not the intent.
  assert.ok(SANDBOX.includes("sv_freelook 2"), "2 is the YES bit");
  assert.ok(!SANDBOX.includes("sv_freelook 1"), "1 is the NO bit and turns freelook OFF");
  assert.ok(SANDBOX.includes("sv_nofreelook 0"));
  assert.ok(SANDBOX.includes("sv_nofreeaim 0"), "projectiles must follow the crosshair");

  for (const stiller of ["god", "notarget", "kill monsters"]) {
    assert.ok(SANDBOX.includes(stiller), `${stiller} keeps the camera where it was put`);
  }
});

test("the BFG is held long enough to actually fire", () => {
  // It winds up for about a second before the ball leaves. Releasing on the next tic, as a rocket
  // does, produces a capture of a wall the shot never reached and a report that the weapon is broken.
  assert.ok(HOLD_TICS("BFG9000") > 35);
  assert.ok(HOLD_TICS("RocketLauncher") < 35);
});

test("there is exactly one way to drive the engine, and it is this CLI", () => {
  // [rc4l] The harness against re-inventing all of the above.
  //
  // These checks used to live in eight hand-written shell scripts beside this file, plus a fresh
  // one improvised whenever a check needed a shape the eight did not have. Each improvisation
  // repeated the same mistakes -- level not reset, monsters left on, a camera forced inside a wall,
  // a stale log tailed for a result -- because a script in a scratch directory has no tests and no
  // second reader. A capability that is missing belongs in capture.mjs with a subcommand over it.
  //
  // So: no scripts in the tool directory. If this fails, the fix is to port the script's job into a
  // subcommand and delete it, not to widen the list.
  const strays = fs.readdirSync(fuactlDir).filter((f) => f.endsWith(".sh"));
  assert.deepEqual(strays, [], `drive the engine through fuactl, not: ${strays.join(", ")}`);
});

test("the tic counter is read from gametic, and a missing one is an error not a NaN", () => {
  // [rc4l] This field name was guessed, and a guess that misses produces NaN rather than a failure:
  // the wait then never advanced and every capture died with an unexplained 30-second timeout.
  // leveltime is the wrong one regardless -- it restarts at zero on a map change, so a wait spanning
  // one sits forever watching a counter that went backwards.
  assert.equal(ticOf({ gametic: 3088, leveltime: 714, inlevel: true }), 3088);
  assert.throws(() => ticOf({ leveltime: 714 }), /gametic/);
  assert.throws(() => ticOf(null), /gametic/);
});

test("sleep waits about as long as it is asked, and a slow command waits longer than a quick one", async () => {
  const started = Date.now();
  await sleep(20);
  assert.ok(Date.now() - started >= 15);
  assert.ok(SLOW_COMMAND.quietMs > 220 && SLOW_COMMAND.maxMs > 4000);
});

// [rc4l] An engine that answers each console line with what `answer` gives, counting the lines asked.
function answering(answer) {
  let handler = null, asked = 0;
  return {
    asked: () => asked,
    onEvent(h) { handler = h; return () => { handler = null; }; },
    async rpc(_method, { text }) { asked++; if (handler) handler("out", { text: answer(text) }); return {}; },
  };
}

const REPORT = (renderer, api, vulkan, standalone) =>
  `MCP_RENDERER\nrenderer=${renderer}\nbackend_api=${api}\n"fua_vulkan" is "${vulkan}"\n"fua_dg_standalone" is "${standalone}"\n`;

test("a launch's mode is read from dumprenderer and fua_vulkan: no GL is Vulkan or WebGPU alone, GL with the backend on is GL beside", () => {
  assert.equal(launchModeOf(REPORT("vulkan", "Vulkan", "true", "true")), "vulkan");
  assert.equal(launchModeOf(REPORT("vulkan", "WebGPU", "true", "true")), "webgpu");
  assert.equal(launchModeOf(REPORT("opengl", "Vulkan", "true", "false")), "beside");
  assert.equal(launchModeOf(REPORT("opengl", "Vulkan", "false", "true")), "gl");
  assert.equal(launchModeOf("Unknown command"), null);
  assert.equal(echoedCvar(REPORT("opengl", "Vulkan", "1", "0"), "fua_dg_standalone"), "0");
  assert.equal(echoedCvar("", "fua_vulkan"), null);
  assert.equal(RENDERER_PROBE, "dumprenderer; fua_vulkan; fua_dg_standalone");
});

test("a GL frame exists for GL alone or GL drawing beside the backend, and otherwise the reason says how to get one", () => {
  assert.deepEqual(glFrameOf(REPORT("opengl", "Vulkan", "false", "true")), { ok: true, why: "" });
  assert.deepEqual(glFrameOf(REPORT("opengl", "Vulkan", "true", "false")), { ok: true, why: "" });
  const standalone = glFrameOf(REPORT("opengl", "Vulkan", "true", "true"));
  assert.equal(standalone.ok, false);
  assert.match(standalone.why, /fua_dg_standalone 1/);
  const none = glFrameOf(REPORT("vulkan", "Vulkan", "true", "true"));
  assert.equal(none.ok, false);
  assert.match(none.why, /no GL \(vulkan alone\).*--gl-beside/);
  assert.match(glFrameFor(null, false).why, /no renderer reported/);
});

test("a connection's mode is asked once, except GL beside, whose standalone cvar can change", async () => {
  const vk = answering(() => REPORT("vulkan", "Vulkan", "true", "true"));
  assert.equal((await rendererReport(vk)).mode, "vulkan");
  assert.equal((await rendererReport(vk)).gl.ok, false);
  assert.equal(vk.asked(), 1);
  let standalone = "true";
  const beside = answering(() => REPORT("opengl", "Vulkan", "true", standalone));
  assert.equal((await rendererReport(beside)).gl.ok, false);
  standalone = "false";
  assert.equal((await rendererReport(beside)).gl.ok, true);
  assert.equal(beside.asked(), 2);
  const silent = answering(() => "");
  assert.equal((await rendererReport(silent)).mode, null);
  await rendererReport(silent);
  assert.equal(silent.asked(), 2, "an engine that named no renderer is asked again");
});
