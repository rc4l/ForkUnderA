import { test } from "node:test";
import assert from "node:assert/strict";
import { FPS_MODES, fpsModes, fpsRow, formatFpsRow, fpsShot, runFps } from "../src/fps.mjs";

const report = (p50, p99) => ({ total: { p50_ms: p50, p99_ms: p99 } });

test("the modes default to all three in order, keep a flag's order under their own names, and refuse one that does not exist", () => {
  assert.deepEqual(fpsModes(undefined), ["gl", "beside", "vulkan"]);
  assert.deepEqual(fpsModes(true), ["gl", "beside", "vulkan"]);
  assert.deepEqual(fpsModes("embed,vulkan"), ["beside", "vulkan"]);   // [rc4l] the old name for GL beside
  assert.deepEqual(fpsModes("vulkan, gl"), ["vulkan", "gl"]);
  assert.throws(() => fpsModes("gl,dx12"), /unknown fps mode dx12/);
  assert.deepEqual(FPS_MODES.vulkan, {});   // [rc4l] a launch runs as players do unless it asks for GL
  assert.deepEqual(FPS_MODES.beside, { glBeside: true });
  assert.deepEqual(FPS_MODES.gl, { gl: true });
});

test("a row is the median round, its frame rate, and the refresh it sits on", () => {
  const row = fpsRow("embed", "opengl", [report(1.089, 2.18), report(1.009, 2.48), report(1.036, 2.1)]);
  assert.equal(row.p50_ms, 1.036);
  assert.equal(row.p99_ms, 2.18);
  assert.equal(row.fps, 965);
  assert.equal(row.boundHz, null);
  assert.deepEqual(row.rounds, [1.089, 1.009, 1.036]);
  assert.equal(fpsRow("vulkan", "vulkan", [report(2.081, 2.3)]).boundHz, 480);
});

test("a row prints its numbers, the refresh note when capped, and its shot or that the shot failed", () => {
  const capped = formatFpsRow({ ...fpsRow("vulkan", "vulkan", [report(2.081, 2.3)]), shot: "a.png" });
  assert.match(capped, /^vulkan vulkan  p50 2\.081 ms  ~481 fps  p99 2\.300 ms  \(rounds 2\.081\)/);
  assert.match(capped, /480 Hz refresh/);
  assert.match(capped, /shot a\.png$/);
  const free = formatFpsRow({ ...fpsRow("gl", null, [report(6.33, 7.9)]), shot: null });
  assert.match(free, /^gl     \?       p50 6\.330/);
  assert.doesNotMatch(free, /refresh/);
  assert.match(free, /shot FAILED$/);
  assert.doesNotMatch(formatFpsRow(fpsRow("gl", "opengl", [report(6.33, 7.9)])), /shot/);
});

test("GL alone is shot from its own buffer and the backend modes from the swapchain, beside the engine", () => {
  assert.deepEqual(fpsShot("gl", "F:/e/forkundera.exe", "run"), { file: "F:/e/sweep/fps_run_gl.png", command: "screenshot sweep/fps_run_gl" });
  assert.deepEqual(fpsShot("embed", "F:/e/forkundera.exe", "run"),
    { file: "F:/e/sweep/fps_run_embed.png", command: 'fua_diligent_shot "F:/e/sweep/fps_run_embed.png"' });
});

test("each mode is its own launch, warmed and measured round by round, then shot and sampled", async () => {
  const launches = [];
  const commands = [];
  const captures = [];
  const lines = [];
  let p50 = 2.0;
  const deps = {
    withInstance: async (o, body) => { launches.push(o); return body({}, { engine: "F:/e/forkundera.exe", pid: 7 }); },
    exec: async (_c, text) => { commands.push(text); return text === "dumprenderer" ? "renderer=vulkan\n" : ""; },
    capture: async (_c, o) => { captures.push(o); p50 += 0.1; return report(p50, p50 + 1); },
    shoot: async (_c, command, file) => (command.startsWith("fua_diligent_shot") ? { path: file } : Promise.reject(new Error("no png"))),
    sample: async (pid, o) => ({ functions: [`pid ${pid} for ${o.seconds}s`] }),
    log: (l) => lines.push(l),
  };
  const rows = await runFps({ modes: ["gl", "vulkan"], rounds: 2, frames: 600, warmup: 300, vsync: false, shot: true, sample: 3, label: "t",
    launch: { map: "MAP10", port: 7799 } }, deps);
  assert.deepEqual(launches, [{ map: "MAP10", port: 7799, gl: true }, { map: "MAP10", port: 7799 }]);
  assert.deepEqual(commands.slice(0, 2), ["vid_vsync 0", "dumprenderer"]);
  assert.deepEqual(captures.slice(0, 3), [{ frames: 600, warmup: 300 }, { frames: 600, warmup: 60 }, { frames: 600, warmup: 60 }]);
  assert.equal(rows[0].p50_ms, 2.25);   // [rc4l] the warm-up round's 2.1 is thrown away; the median of 2.2 and 2.3 is kept
  assert.equal(rows[0].shot, null);
  assert.equal(rows[1].shot, "F:/e/sweep/fps_t_vulkan.png");
  assert.deepEqual(rows[1].sample, ["pid 7 for 3s"]);
  assert.equal(lines.length, 2);
  const quiet = await runFps({ modes: ["embed"], rounds: 1, frames: 10, warmup: 0, vsync: true, shot: false, launch: {} },
    { ...deps, log: undefined });
  assert.equal(quiet[0].shot, undefined);
  assert.equal(quiet[0].sample, undefined);
  assert.ok(commands.includes("vid_vsync 1"));
});
