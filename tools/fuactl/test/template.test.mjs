import { test } from "node:test";
import assert from "node:assert/strict";
import { directRunArgs, compareCounter, compareEachLaunch, compareNetplay, withConfigCvars, compareOverTime, compareQuit, compareSameFrame, compareSequence, compareSoak, judge, judgeCounter, judgeQuit, judgeSoak, judgeWindowPoint, judgeNoteBeside, scenarioOutcome, isMain, sideCommands, vantageCommands, boxWithin, runScenarioIfMain, scenarioOptions, scenarioRun, templateScenarioFiles, probeChoices, pairFrame, withSideCvars, withEverySideCvars, matrixSets, matrixAdjust, launchRenderer, sideAdjust, sideBesideGl, webgpuOptions, launchBackendApi, writeTemplateFile, WEBGPU_CEILING, GL_VS_VULKAN } from "../src/template.mjs";
import { pathToFileURL } from "node:url";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

// [rc4l] A decoded-frame stand-in: w x h, every pixel the same RGB.
function flat(w, h, rgb) {
  const data = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) data.set(rgb, i * 3);
  return { w, h, channels: 3, colorType: 2, data };
}

const BOXES = { sky: { box: [0, 0, 1, 0.5], ceiling: 6 } };

// [rc4l] #33's white sprite lasted one frame, so its template reads the engine's count rather than a picture.
test("a counter passes at or under its ceiling, and fails over it or when the engine never printed it", () => {
  const counter = { label: "slots past the fill", pattern: "(\\d+) since load", ceiling: 0 };
  assert.deepEqual(judgeCounter("MAP01", "dyn slots: 2 past the fill (0 since load)", counter),
    { line: "PASS MAP01 slots past the fill: 0 (ceiling 0)", failed: 0 });
  assert.deepEqual(judgeCounter("MAP01", "dyn slots: 2 past the fill (14 since load)", counter),
    { line: "FAIL MAP01 slots past the fill: 14 (ceiling 0)", failed: 1 });
  assert.deepEqual(judgeCounter("MAP01", "Unknown command", counter),
    { line: "FAIL MAP01: no slots past the fill in the engine's answer", failed: 1 });
});

test("a counter with a floor passes at or over it and fails under it", () => {
  const counter = { label: "particles drawn", pattern: "particles: (\\d+) drawn", floor: 1 };
  assert.deepEqual(judgeCounter("MAP01", "particles: 40 drawn this frame", counter),
    { line: "PASS MAP01 particles drawn: 40 (floor 1)", failed: 0 });
  assert.deepEqual(judgeCounter("MAP01", "particles: 0 drawn this frame", counter),
    { line: "FAIL MAP01 particles drawn: 0 (floor 1)", failed: 1 });
});

test("the renderer is read from dumprenderer's report, and none when it printed none", () => {
  assert.equal(launchRenderer("MCP_RENDERER\nrenderer=vulkan\nscreen_width=1280\n"), "vulkan");
  assert.equal(launchRenderer("MCP_RENDERER\nrenderer=opengl\n"), "opengl");
  assert.equal(launchRenderer("Unknown command"), null);
  assert.equal(launchRenderer(undefined), null);
});

test("a box under its ceiling passes and one over it fails", () => {
  const frames = {
    same: { gl: flat(32, 32, [10, 10, 10]), vk: flat(32, 32, [12, 12, 12]) },
    wall: { gl: flat(32, 32, [10, 10, 10]), vk: flat(32, 32, [200, 10, 10]) },
  };
  const r = judge("MAP01", frames, BOXES, [32, 32]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^PASS MAP01 same/);
  assert.match(r.lines[1], /^FAIL MAP01 wall/);
});

test("a missing capture is a failure, never a pass", () => {
  const r = judge("MAP01", { gone: { gl: flat(32, 32, [0, 0, 0]), vk: null } }, BOXES, [32, 32]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /capture failed/);
});

test("frames of the wrong size fail rather than compare the wrong pixels", () => {
  const r = judge("MAP01", { small: { gl: flat(16, 16, [0, 0, 0]), vk: flat(16, 16, [0, 0, 0]) } }, BOXES, [32, 32]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /boxes assume 32x32/);
});

test("a box that names its vantage is judged only at that vantage", () => {
  const frames = { step: { gl: flat(32, 32, [0, 0, 0]), vk: flat(32, 32, [90, 0, 0]) },
    lintel: { gl: flat(32, 32, [0, 0, 0]), vk: flat(32, 32, [0, 0, 0]) } };
  const r = judge("MAP01", frames, { gap: { box: [0, 0, 1, 1], ceiling: 1, vantage: "lintel" } }, [32, 32]);
  assert.equal(r.failed, 0);
  assert.equal(r.lines.length, 1);
  assert.match(r.lines[0], /^PASS MAP01 lintel/);
});

test("a scenario imported rather than run keeps its options for probing", () => {
  const url = "file:///scenarios/imported.mjs";
  runScenarioIfMain(url, () => ({ name: "imported" }));
  assert.equal(scenarioOptions(url)().name, "imported");
  assert.equal(scenarioOptions("file:///scenarios/never.mjs"), null);
});

test("a scenario run directly takes its port bare and its engine by --engine", () => {
  assert.deepEqual(directRunArgs(["7850", "--engine", "F:/b/forkundera.exe"]), { port: 7850, engine: "F:/b/forkundera.exe" });
  assert.deepEqual(directRunArgs(["--engine", "e.exe"]), { port: null, engine: "e.exe" });
  assert.deepEqual(directRunArgs(["--engine"]), { port: null, engine: null });
});

test("an imported scenario runs as one call through the comparison it named", async () => {
  const url = "file:///scenarios/runnable.mjs";
  const seen = [];
  runScenarioIfMain(url, () => ({ name: "runnable", port: 7 }), async (opts) => { seen.push(opts.port); return { lines: ["PASS a"], failed: 0 }; });
  const out = await scenarioRun(url)();
  assert.deepEqual(seen, [7]);
  assert.deepEqual(out, { lines: ["PASS a", "all runnable checks within their limits"], code: 0 });
  assert.equal(scenarioRun("file:///scenarios/never.mjs"), null);
  await scenarioRun(url, (o) => ({ ...o, port: 9 }))();
  assert.deepEqual(seen, [7, 9]);
});

// [rc4l] Queue #42's test was shown to catch the old behaviour by running its blanked side with fua_gl_meshdraw 1.
test("one side of a scenario takes extra cvars, and a side that is not there is named", () => {
  const opts = { name: "glplain", sides: [{ name: "gl", gl: true }, { name: "blanked", cvars: { a: 1 }, commands: ["map MAP01"] }] };
  const out = withSideCvars(opts, "blanked", { fua_gl_meshdraw: "1" });
  assert.deepEqual(out.sides[1], { name: "blanked", cvars: { a: 1, fua_gl_meshdraw: "1" }, commands: ["map MAP01"] });
  assert.equal(out.sides[0], opts.sides[0]);
  assert.deepEqual(opts.sides[1].cvars, { a: 1 });
  assert.deepEqual(withSideCvars({ name: "x" }, "vk", { b: 2 }).sides, [GL_VS_VULKAN[0], { ...GL_VS_VULKAN[1], cvars: { b: 2 } }]);
  assert.throws(() => withSideCvars(opts, "nope", {}), /glplain has no side nope \(gl, blanked\)/);
});

test("only files that register a template scenario are run, all of them or those named", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-scen-"));
  fs.writeFileSync(path.join(dir, "a.mjs"), "runScenarioIfMain(import.meta.url, () => ({}));");
  fs.writeFileSync(path.join(dir, "b.mjs"), "runScenarioIfMain(import.meta.url, () => ({}));");
  fs.writeFileSync(path.join(dir, "measure.mjs"), "await launchSomething();");
  fs.writeFileSync(path.join(dir, "bench.json"), "{}");
  try {
    assert.deepEqual(templateScenarioFiles(dir).sort(), ["a.mjs", "b.mjs"]);
    assert.deepEqual(templateScenarioFiles(dir, ["b", "measure"]), ["b.mjs"]);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("a probe takes the named map, vantage and side, else the first map, vantage and Vulkan side", () => {
  const opts = { maps: ["MAP01", "MAP02"], vantages: [{ name: "a" }, { name: "b" }],
    sides: [{ name: "gl", gl: true }, { name: "vk" }] };
  const plain = probeChoices(opts);
  assert.deepEqual([plain.map, plain.vantage.name, plain.side.name], ["MAP01", "a", "vk"]);
  const named = probeChoices(opts, { map: "MAP02", vantage: "b", side: "gl" });
  assert.deepEqual([named.map, named.vantage.name, named.side.name], ["MAP02", "b", "gl"]);
  assert.equal(probeChoices({ maps: ["MAP01"], vantages: [{ name: "a" }], sides: [{ name: "only", gl: true }] }).side.name, "only");
  assert.throws(() => probeChoices(opts, { vantage: "z" }), /no vantage z/);
  assert.throws(() => probeChoices(opts, { side: "z" }), /no side z/);
  assert.throws(() => probeChoices(opts, { map: "MAP09" }), /no map MAP09/);
});

test("a scenario's own sides are compared by name", () => {
  const frames = { far: { flat: flat(32, 32, [40, 40, 40]), clustered: flat(32, 32, [0, 0, 0]) } };
  const r = judge("MAP01", frames, BOXES, [32, 32], ["flat", "clustered"]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^FAIL MAP01 far/);
});

test("a scenario exits 0 only when nothing failed, and 3 when it refused to run", () => {
  assert.deepEqual(scenarioOutcome("x", { lines: ["PASS a"], failed: 0 }), { lines: ["PASS a", "all x checks within their limits"], code: 0 });
  assert.equal(scenarioOutcome("x", { lines: [], failed: 2 }).code, 1);
  assert.equal(scenarioOutcome("x", { lines: [], failed: 1, refused: true }).code, 3);
});

test("a scenario counts as main only when node was started on that very file", () => {
  const url = pathToFileURL("/repo/scenarios/a.mjs").href;
  assert.equal(isMain(url, "/repo/scenarios/a.mjs"), true);
  assert.equal(isMain(url, "/repo/test/a.test.mjs"), false);
  assert.equal(isMain(url, undefined), false);
});

test("a side sets its cvars first and then runs its commands, in order", () => {
  assert.deepEqual(sideCommands({ cvars: { fua_surface_bands: 0, gl_lights: 1 }, commands: ["fua_mapbake_now"] }),
    ["fua_surface_bands 0", "gl_lights 1", "fua_mapbake_now"]);
  assert.deepEqual(sideCommands({ name: "gl", gl: true }), []);
  assert.deepEqual(vantageCommands({ atVantage: ["fua_mesh_blank"] }), ["fua_mesh_blank"]);
  assert.deepEqual(vantageCommands({ name: "gl", gl: true }), []);
});

test("a box with a floor must change by at least that much, and one with a ceiling by no more", () => {
  assert.equal(boxWithin(0.0, { floor: 1 }), false);
  assert.equal(boxWithin(2.4, { floor: 1 }), true);
  assert.equal(boxWithin(0.0, { ceiling: 0.05 }), true);
  assert.equal(boxWithin(0.3, { ceiling: 0.05 }), false);
  const frames = { pool: { before: flat(32, 32, [10, 10, 10]), after: flat(32, 32, [10, 10, 10]) } };
  const r = judge("MAP01", frames, { water: { box: [0, 0, 1, 1], floor: 1 } }, [32, 32], ["before", "after"]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^FAIL MAP01 pool .* floor 1$/);
});

test("a box can be judged per pixel, where block means would hide a ripple", () => {
  const stripes = (phase) => ({ w: 32, h: 32, channels: 3, colorType: 2,
    data: Buffer.from(Array.from({ length: 32 * 32 }, (_, i) => ((i + phase) % 2 ? [200, 200, 200] : [0, 0, 0])).flat()) });
  const frames = { pool: { before: stripes(0), after: stripes(1) } };
  assert.equal(judge("MAP01", frames, { water: { box: [0, 0, 1, 1], floor: 1 } }, [32, 32], ["before", "after"]).failed, 1);
  assert.equal(judge("MAP01", frames, { water: { box: [0, 0, 1, 1], floor: 1, block: 1 } }, [32, 32], ["before", "after"]).failed, 0);
});

// [rc4l] WDI18's mirror frame was wrong only in GL's half of a Vulkan session, where GL draws replayed walls from the backend's mesh.
test("a Vulkan side keeps Vulkan's frame of the pair, or GL's when it asks for it", () => {
  const pair = { gl: "a_gl.png", vk: "a_vk.png" };
  assert.equal(pairFrame({ name: "vk" }, pair), "a_vk.png");
  assert.equal(pairFrame({ name: "glmesh", frame: "gl" }, pair), "a_gl.png");
});

test("a run-wide side change skips a scenario without that side instead of ending the run", async () => {
  const opts = { name: "x", sides: [{ name: "gl", gl: true }, { name: "vk" }] };
  assert.deepEqual(sideAdjust("vk", { a: "1" })(opts).sides[1].cvars, { a: "1" });
  assert.equal(sideAdjust("blanked", { a: "1" })(opts), null);
  const url = "file:///scenarios/sideless.mjs";
  let ran = false;
  runScenarioIfMain(url, () => ({ name: "sideless", sides: [{ name: "flat" }, { name: "clustered" }] }), async () => { ran = true; return { lines: [], failed: 0 }; });
  const out = await scenarioRun(url, sideAdjust("vk", {}))();
  assert.equal(ran, false);
  assert.equal(out.skipped, true);
  assert.equal(out.code, 0);
  assert.match(out.lines[0], /skipped: no side to run \(flat, clustered\)/);
  const why = await scenarioRun(url, () => ({ skip: "it needs ray tracing" }))();
  assert.deepEqual(why, { lines: ["skipped: it needs ray tracing"], code: 0, skipped: true });
});

test("the backend's API is read from dumprenderer", () => {
  assert.equal(launchBackendApi("MCP_RENDERER\nrenderer=vulkan\nbackend_api=WebGPU\n"), "WebGPU");
  assert.equal(launchBackendApi("renderer=opengl\n"), null);
  assert.equal(launchBackendApi(undefined), null);
});

test("a WebGPU run launches the Vulkan side on each API without GL, every box at one ceiling", () => {
  const opts = {
    name: "s",
    sides: [{ name: "gl", gl: true }, { name: "vk", gl: false, glDraws: true, cvars: { r_x: 1 } }],
    boxes: { MAP01: { wall: { box: [0, 0, 8, 8], ceiling: 9, floor: 3, block: 8, vantage: "a" } } },
  };
  const w = webgpuOptions(opts);
  assert.deepEqual(w.sides, [
    { name: "vk", gl: false, cvars: { r_x: 1 }, noGl: true },
    { name: "wgpu", gl: false, cvars: { r_x: 1 }, noGl: true, webgpu: true },
  ]);
  assert.deepEqual(w.boxes.MAP01.wall, { box: [0, 0, 8, 8], block: 8, vantage: "a", ceiling: WEBGPU_CEILING });
  assert.equal(webgpuOptions({ name: "glonly", sides: [{ name: "gl", gl: true }, { name: "vk2", frame: "gl" }], boxes: {} }), null);
  assert.equal(webgpuOptions({ name: "pair", boxes: {} }).sides[0].name, "vk");
});

test("a WebGPU run of a scenario judged against itself runs on WebGPU alone with its own limits", () => {
  const w = webgpuOptions({ name: "particles", counter: { command: "c" }, sides: [{ name: "standalone", cvars: { s: 1 } }] }, compareCounter);
  assert.deepEqual(w.sides, [{ name: "wgpu", cvars: { s: 1 }, noGl: true, webgpu: true }]);
  assert.equal(w.boxes, undefined);
  const boxes = { MAP01: { moving: { box: [0, 0, 1, 1], floor: 1 } } };
  const t = webgpuOptions({ name: "scroll", boxes }, compareOverTime);
  assert.deepEqual(t.sides, [{ name: "wgpu", gl: false, noGl: true, webgpu: true }]);
  assert.equal(t.boxes, boxes);
});

// [rc4l] Queue #46: a quit crashed in the frame fence's static destructor, which printed to a console already freed.
test("a quit passes only when the engine left cleanly", () => {
  assert.deepEqual(judgeQuit("MAP01 vk", { gone: { code: 0 }, crashed: false }), { line: "PASS MAP01 vk: quit cleanly", failed: 0 });
  assert.deepEqual(judgeQuit("MAP01 vk", { gone: { code: 3221225477 }, crashed: true }), { line: "FAIL MAP01 vk: the engine crashed on its way out", failed: 1 });
  assert.deepEqual(judgeQuit("MAP01 vk", { gone: null, crashed: false }), { line: "FAIL MAP01 vk: the engine did not exit", failed: 1 });
  assert.deepEqual(judgeQuit("MAP01 vk", { gone: { code: 1 }, crashed: false }), { line: "FAIL MAP01 vk: the engine exited with 1", failed: 1 });
  assert.deepEqual(judgeQuit("MAP01 vk", { gone: { code: null, signal: "SIGKILL" }, crashed: false }), { line: "FAIL MAP01 vk: the engine exited with SIGKILL", failed: 1 });
});

// [rc4l] The backend left window pixels unscaled, so at vid_scalefactor 0.5 every menu hover landed twice as far in.
test("a window point passes only at the render pixel under it, in a frame the scale took", () => {
  const want = { from: [1000, 600], x: 500, y: 300, width: 640, height: 360 };
  assert.deepEqual(judgeWindowPoint("MAP01 vk", { x: 500, y: 301, width: 640, height: 360 }, want), { line: "PASS MAP01 vk: window 1000,600 at render 500,301 of 640x360, want 500,300 of 640x360", failed: 0 });
  assert.equal(judgeWindowPoint("MAP01 vk", { x: 1000, y: 600, width: 640, height: 360 }, want).failed, 1);
  assert.equal(judgeWindowPoint("MAP01 vk", { x: 500, y: 300, width: 1280, height: 720 }, want).failed, 1, "the scale never took");
  assert.equal(judgeWindowPoint("MAP01 vk", { x: 500, y: 300, width: 640, height: 400 }, want).failed, 1);
});

// [rc4l] The renderer's "changes on the next launch" took a line of its own; the restart note belongs beside the row, and only once it was changed.
test("a note passes only beside its row and only while the row is changed", () => {
  const read = (...rows) => ({ texts: rows.flatMap(([y, ...parts]) => parts.map(([x, text]) => ({ x, y, w: 10, text }))) });
  const spec = { row: "Renderer", note: "(restart", absent: ["next launch"] };
  const plain = read([100, [300, "Fullscreen"], [500, "Yes"]], [130, [300, "Renderer"], [500, "Vulkan (Recommended)"]], [160, [300, "Aspect"]]);
  const noted = read([100, [300, "Fullscreen"]], [130, [300, "Renderer"], [500, "GL (Legacy)"], [700, "(restart the game to apply)"]], [160, [300, "Aspect"]]);
  assert.deepEqual(judgeNoteBeside("MAP01 vk", { before: plain, changed: noted, back: plain }, spec),
    { line: 'PASS MAP01 vk: "(restart" beside "Renderer" only while changed', failed: 0 });
  const centred = read([130, [300, "Renderer"], [500, "Vulkan (Recommended)"]], [139, [900, "(restart)"]], [160, [300, "Aspect"]]);
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: centred, back: plain }, spec).failed, 0, "a smaller note centred on the row");
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: plain, back: plain }, spec).line, "FAIL MAP01 vk: no note after the change");
  const ownLine = read([130, [300, "Renderer"], [500, "GL (Legacy)"]], [160, [300, "(restart the game to apply)"]]);
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: ownLine, back: plain }, spec).line, "FAIL MAP01 vk: the note is on a line of its own");
  const over = { texts: [{ x: 300, y: 130, w: 60, text: "Renderer" }, { x: 500, y: 130, w: 330, text: "GL (Legacy)" }, { x: 800, y: 130, w: 400, text: "(restart to apply)" }] };
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: over, back: plain }, spec).line, "FAIL MAP01 vk: the note overlaps the row");
  const clear = { texts: [{ x: 300, y: 130, w: 60, text: "Renderer" }, { x: 500, y: 130, w: 300, text: "GL (Legacy)" }, { x: 800, y: 130, w: 400, text: "(restart to apply)" }] };
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: clear, back: plain }, spec).failed, 0, "touching is not overlapping");
  const leftOf = read([130, [300, "Renderer"], [200, "(restart)"]]);
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: leftOf, back: plain }, spec).failed, 1);
  assert.equal(judgeNoteBeside("MAP01 vk", { before: noted, changed: noted, back: noted }, spec).line,
    "FAIL MAP01 vk: the note shows before any change; the note still shows after changing back");
  assert.equal(judgeNoteBeside("MAP01 vk", { before: read(), changed: read(), back: read() }, spec).line, 'FAIL MAP01 vk: no "Renderer" row');
  const old = read([130, [300, "Renderer"], [500, "GL (Legacy)"], [700, "(restart to apply)"]], [160, [300, "(the renderer changes on the next launch)"]]);
  assert.equal(judgeNoteBeside("MAP01 vk", { before: plain, changed: old, back: plain }, spec).line, 'FAIL MAP01 vk: "next launch" still shows');
  assert.equal(judgeNoteBeside("MAP01 vk", { before: {}, changed: noted, back: {} }, { row: "Renderer", note: "(restart" }).failed, 0);
});

// [rc4l] A new game leaked every player's userinfo cvars, about 1,400 allocations a level load, until a soak named them.
test("a soak passes only when the engine judged counters and none kept growing", () => {
  assert.deepEqual(judgeSoak("MAP01 vk", { pass: true, marks: 6, judged: 44, grew: [] }), { line: "PASS MAP01 vk: 44 counters held over 6 marks", failed: 0 });
  assert.deepEqual(judgeSoak("MAP01 vk", { pass: false, marks: 2, judged: 0 }), { line: "FAIL MAP01 vk: judged nothing (2 marks)", failed: 1 });
  const grew = [{ name: "mem/heap_bytes", text: "mem/heap_bytes grew 700 KB a cycle (3.4 MB over 5 cycles)" }, { name: "census/actors", text: "census/actors grew 8.00 a cycle (40.00 over 5 cycles)" }];
  assert.deepEqual(judgeSoak("MAP01 vk", { pass: false, marks: 6, judged: 44, grew }),
    { line: "FAIL MAP01 vk: kept growing: mem/heap_bytes grew 700 KB a cycle (3.4 MB over 5 cycles); census/actors grew 8.00 a cycle (40.00 over 5 cycles)", failed: 1 });
});

test("only the comparisons that judge a launch against itself are marked so", () => {
  assert.ok(compareCounter.judgedAlone && compareOverTime.judgedAlone && compareSequence.judgedAlone && compareQuit.judgedAlone && compareSoak.judgedAlone && compareEachLaunch.judgedAlone);
  assert.ok(!compareSameFrame.judgedAlone);
});

test("a WebGPU run of a quit runs on WebGPU alone", () => {
  const w = webgpuOptions({ name: "quitclean", sides: [{ name: "vk", noGl: true, launchCvars: { v: 1 } }] }, compareQuit);
  assert.deepEqual(w.sides, [{ name: "wgpu", noGl: true, webgpu: true, launchCvars: { v: 1 } }]);
});

test("a WebGPU run skips what WebGPU cannot do: GL beside it in one frame, or ray tracing", () => {
  assert.match(webgpuOptions({ name: "sameframe", boxes: {} }, compareSameFrame).skip, /a WebGPU launch has no GL/);
  assert.match(webgpuOptions({ name: "rt", boxes: {}, needs: ["raytracing"] }).skip, /needs ray tracing/);
  assert.match(webgpuOptions({ name: "wipe", boxes: {}, needs: ["readback"] }, compareEachLaunch).skip, /reads each presented frame back/);
});

// [rc4l] A capture sequence names its frames, and each box the two it is judged between.
test("a box naming a pair of captures is judged between those two", () => {
  const frames = { side: { clean: flat(32, 32, [10, 10, 10]), marked: flat(32, 32, [60, 60, 60]), resized: flat(32, 32, [10, 10, 10]) } };
  const boxes = {
    made: { box: [0, 0, 1, 1], pair: ["clean", "marked"], floor: 2 },
    kept: { box: [0, 0, 1, 1], pair: ["marked", "resized"], ceiling: 1 },
  };
  const r = judge("MAP01", frames, boxes, [32, 32], ["clean", "marked"]);
  assert.equal(r.failed, 1);
  assert.match(r.lines[0], /^PASS MAP01 side\s+made/);
  assert.match(r.lines[1], /^FAIL MAP01 side\s+kept/);
  const missing = judge("MAP01", { side: { clean: frames.side.clean, marked: frames.side.marked } }, boxes, [32, 32], ["clean", "marked"]);
  assert.equal(missing.failed, 1);
  assert.equal(missing.lines[1], "FAIL MAP01 side kept: no capture marked or resized");
});

test("a WebGPU run of a capture sequence runs on WebGPU alone with its boxes as they are", () => {
  const boxes = { MAP01: { kept: { box: [0, 0, 1, 1], pair: ["a", "b"], ceiling: 1 } } };
  const w = webgpuOptions({ name: "sequence", boxes }, compareSequence);
  assert.deepEqual(w.sides, [{ name: "wgpu", gl: false, noGl: true, webgpu: true }]);
  assert.equal(w.boxes, boxes);
});

test("a matrix names one cvar set per value, each laid on every side over the run's own adjustment", () => {
  assert.deepEqual(matrixSets("fua_gamethread=0,1"), [{ fua_gamethread: "0" }, { fua_gamethread: "1" }]);
  assert.throws(() => matrixSets("nonsense"), /cvar=a,b/);
  const opts = { name: "t", sides: [{ name: "gl", gl: true }, { name: "vk", gl: false, cvars: { x: "2" } }] };
  const every = withEverySideCvars(opts, { y: "1" });
  assert.deepEqual(every.sides.map((s) => s.cvars), [{ y: "1" }, { x: "2", y: "1" }]);
  assert.deepEqual(withEverySideCvars({ name: "d" }, { y: "1" }).sides.map((s) => s.cvars.y), GL_VS_VULKAN.map(() => "1"));
  const run = matrixAdjust({ y: "1" }, sideAdjust("vk", { z: "3" }));
  assert.deepEqual(run(opts).sides[1].cvars, { x: "2", z: "3", y: "1" });
  assert.equal(matrixAdjust({ y: "1" }, () => null)(opts), null);
  assert.deepEqual(matrixAdjust({ y: "1" }, () => ({ skip: "why" }))(opts), { skip: "why" });
  assert.deepEqual(matrixAdjust({ y: "1" })(opts).sides[0].cvars, { y: "1" });
});

// [rc4l] Only a side judged against GL in one frame runs the backend beside GL; every other Vulkan side runs as players do.
test("a side runs the backend beside GL only when GL's frame or both renderers' one frame judge it, or it asks", () => {
  assert.equal(sideBesideGl({ name: "vk" }), false);
  assert.equal(sideBesideGl({ name: "vk", noGl: true }), false);
  assert.equal(sideBesideGl({ name: "standalone", cvars: { fua_dg_standalone: 1 } }), false);
  assert.equal(sideBesideGl({ name: "blanked", frame: "gl" }), true);
  assert.equal(sideBesideGl({ name: "pair", glDraws: true }), true);
  assert.equal(sideBesideGl({ name: "x", glBeside: true }), true);
  assert.equal(sideBesideGl({ name: "gl", gl: true, frame: "gl" }), false, "GL alone has no backend to sit beside");
  assert.equal(sideBesideGl({ name: "wgpu", webgpu: true, glDraws: true }), false, "a WebGPU launch has no GL");
  assert.equal(sideBesideGl(), false);
});

test("a netplay scenario hosts each configuration on each map, judges the set and always closes it", async () => {
  const launched = [], closed = [];
  const deps = {
    launch: async (o) => { launched.push(o); if (o.lag === "bad") throw new Error("never joined"); return { id: launched.length }; },
    close: async (set) => closed.push(set.id),
    seams: async (set) => (set.id === 1 ? ["observer player 1: 2 seam violations, the first in net/parse (game)"] : []),
    replication: async (set) => (set.id === 1 ? "the server sent 3 changed rows a tic late, untouched, the first sector 4 (families 2)" : ""),
  };
  const configs = [{ name: "lan", players: 1, observers: 1, broadcast: true }, { name: "elsewhere", maps: ["MAP02"] }, { name: "ping", lag: "100", clientCvars: { cl_unlagged: 1 } }, { name: "broken", lag: "bad" }];
  const judged = [];
  const r = await compareNetplay({ name: "nt", wad: Buffer.from("PWAD"), maps: ["MAP01"], configs, judgeNetplay: async (set, label, info) => { judged.push([set.id, label, info.config.name]); return set.id === 2 ? { line: `FAIL ${label}`, failed: 1 } : { lines: [`PASS ${label}`], failed: 0 }; } }, deps);
  assert.deepEqual(judged, [[1, "MAP01 lan", "lan"], [2, "MAP01 ping", "ping"]]);
  assert.deepEqual(r.lines, ["PASS MAP01 lan", "FAIL MAP01 lan: observer player 1: 2 seam violations, the first in net/parse (game)",
    "FAIL MAP01 lan: the server sent 3 changed rows a tic late, untouched, the first sector 4 (families 2)", "FAIL MAP01 ping", "FAIL MAP01 broken: never joined"]);
  assert.equal(r.failed, 4);
  assert.deepEqual(closed, [1, 2]);
  assert.deepEqual([launched[0].players, launched[0].observers, launched[1].players, launched[1].lag, launched[1].clientCvars], [1, 1, 1, "100", { cl_unlagged: 1 }]);
  assert.deepEqual([launched[0].broadcast, launched[1].broadcast], [true, false]);
  assert.equal(compareNetplay.judgedAlone, true);
  assert.match(webgpuOptions({}, compareNetplay).skip, /netgame/);
});

test("a netplay configuration may bring its own file, a pk3 named as one beside the scenario's wad", async () => {
  const launched = [], judgedFiles = [];
  const deps = { launch: async (o) => { launched.push(o); return { id: launched.length }; }, close: async () => {}, seams: async () => [], replication: async () => "" };
  const zip = Buffer.from([0x50, 0x4b, 0x03, 0x04, 1, 2]);
  await compareNetplay({ name: "ntfile", wad: Buffer.from("PWAD"), maps: ["MAP01"], configs: [{ name: "wad" }, { name: "pk3", wad: zip }],
    judgeNetplay: async (set, label, info) => { judgedFiles.push(info.wadPath); return { line: `PASS ${label}`, failed: 0 }; } }, deps);
  assert.deepEqual(launched.map((o) => path.basename(o.files[0])), ["fua-ntfile-template.wad", "fua-ntfile-pk3-template.pk3"]);
  assert.deepEqual(judgedFiles, launched.map((o) => o.files[0]));
  assert.deepEqual(fs.readFileSync(launched[1].files[0]), zip);
});

test("a template file is named for what it holds, a pk3 by its zip header", () => {
  const written = [];
  const write = (file, data) => written.push(data.length);
  assert.equal(writeTemplateFile("a", Buffer.from("PWAD...."), "/t", write), path.join("/t", "fua-a-template.wad"));
  assert.equal(writeTemplateFile("b", Buffer.from([0x50, 0x4b, 3, 4]), "/t", write), path.join("/t", "fua-b-template.pk3"));
  assert.equal(writeTemplateFile("c", Buffer.from("PK"), "/t", write), path.join("/t", "fua-c-template.wad"));
  assert.deepEqual(written, [8, 4, 2]);
});

test("--side names a netplay configuration and its cvars reach that configuration's server and clients", () => {
  const opts = { name: "n", configs: [{ name: "lan" }, { name: "ping", serverCvars: { a: 1 } }] };
  const r = withSideCvars(opts, "ping", { fua_debug_netdrop: "47" });
  assert.deepEqual(r.configs[1], { name: "ping", serverCvars: { a: 1, fua_debug_netdrop: "47" }, clientCvars: { fua_debug_netdrop: "47" } });
  assert.deepEqual(r.configs[0], { name: "lan" });
  assert.throws(() => withConfigCvars(opts, "wan", {}), /no configuration wan \(lan, ping\)/);
  assert.equal(sideAdjust("wan", {})(opts), null);
  assert.equal(sideAdjust("lan", { x: 1 })(opts).configs[0].clientCvars.x, 1);
});
