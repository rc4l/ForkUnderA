import { test } from "node:test";
import assert from "node:assert/strict";
import { ensureCheat, verifyWorld, playCvars, playArgs, sameCvarValue, CHEATS, readView, windowForView, viewForWindow, sameView, fitView } from "../src/play.mjs";
import { engineArgs } from "../src/launch.mjs";

// A stand-in engine: answers console commands from a script of replies, and records what it was asked.
function fakeEngine(replies) {
  const asked = [];
  return {
    asked,
    exec: async (_c, text) => { asked.push(text); const r = replies[text]; return typeof r === "function" ? r() : (r ?? ""); },
  };
}

test("a cheat that is already ON is left on, not toggled off", () => {
  // [rc4l] This is the bug it exists for. `god` is a toggle: issuing it at a session that already
  // had it on turns it OFF. A capture run ended in a death screen that way, with the fault nowhere
  // in the frame, after two separate steps each helpfully "enabling god mode".
  const eng = fakeEngine({ god: "Degreelessness Mode ON\n" });
  return ensureCheat({}, CHEATS[0], eng).then((r) => {
    assert.equal(r.on, true);
    assert.equal(r.flipped, false);
    assert.deepEqual(eng.asked, ["god"], "one issue is enough when it lands on");
  });
});

test("a cheat that lands OFF is issued again until it is on", () => {
  let n = 0;
  const eng = fakeEngine({ god: () => (++n === 1 ? "Degreelessness Mode OFF\n" : "Degreelessness Mode ON\n") });
  return ensureCheat({}, CHEATS[0], eng).then((r) => {
    assert.equal(r.on, true);
    assert.equal(r.flipped, true);
    assert.deepEqual(eng.asked, ["god", "god"]);
  });
});

test("an engine that says neither is reported as unknown, never assumed on", () => {
  const eng = fakeEngine({ god: "" });
  return ensureCheat({}, CHEATS[0], eng).then((r) => assert.equal(r.on, null));
});

test("the world is read BACK, and a wrong value is reported rather than hoped over", async () => {
  const eng = fakeEngine({
    sv_nomonsters: '"sv_nomonsters" is "true"\n',
    sv_freelook:   '"sv_freelook" is "1"\n',     // the NO bit -- freelook is actually OFF here
    sv_nofreeaim:  '"sv_nofreeaim" is "false"\n',
    autoaim:       '"autoaim" is "0"\n',
  });
  const report = await verifyWorld({}, eng);
  const byName = Object.fromEntries(report.map((r) => [r.name, r]));

  assert.equal(byName.sv_nomonsters.ok, true);
  assert.equal(byName.sv_freelook.ok, false, "1 is DF_NO_FREELOOK; it must not pass as enabled");
  assert.equal(byName.sv_freelook.actual, "1");
  assert.equal(byName.autoaim.ok, true);
});

test("a cvar the engine does not answer is a failure, not a pass", async () => {
  const report = await verifyWorld({}, fakeEngine({}));
  assert.ok(report.every((r) => r.ok === false), "silence must never read as agreement");
});

test("the hands-on build asks for no monsters and for freelook enabled", () => {
  const cv = playCvars({});
  assert.equal(cv.sv_nomonsters, 1);
  assert.equal(cv.sv_freelook, 2, "1 would DISABLE freelook");
  assert.equal(cv.sv_nofreeaim, 0);
  // [rc4l] The BFG reads its own dmflags2 bit in A_FireBFG and ignores sv_nofreeaim entirely, so a
  // build can pass every other free-aim check and still auto-aim the one weapon being tested with.
  assert.equal(cv.sv_bfgfreeaim, 1);
  assert.equal(playCvars({ monsters: true }).sv_nomonsters, undefined);
  assert.equal(playCvars({ sideBySide: true }).fua_dg_embed, 0, "both renderers on screen at once");
  // [rc4l] GL draws whenever the backend's window is separate (ComputeFrameRoute), so side by side no longer sets standalone.
  assert.equal(playCvars({ sideBySide: true }).fua_dg_standalone, undefined);
  // [rc4l] A locked launch is always in the background, so its world must not pause there.
  assert.equal(playCvars({ lock: true }).i_pauseinbackground, 0);
  assert.equal(playCvars({}).i_pauseinbackground, undefined);
  // [rc4l] --cvar's come last, so they can turn off what the launch set.
  assert.equal(playCvars({ extra: { sv_nomonsters: "0" } }).sv_nomonsters, "0");
  assert.equal(playCvars({ extra: { fua_dg_standalone: "0" } }).fua_dg_standalone, "0");
});

test("--no-gl launches with -vulkan after the wads, and without it nothing extra", () => {
  assert.deepEqual(playArgs(["a.pk3", "b.wad"]), ["-file", "a.pk3", "-file", "b.wad"]);
  assert.deepEqual(playArgs(["a.pk3"], { noGl: true }), ["-file", "a.pk3", "-vulkan"]);
  assert.deepEqual(playArgs([], { noGl: true }), ["-vulkan"]);
  assert.deepEqual(playArgs(["a.pk3"], { noGl: true, webgpu: true }), ["-file", "a.pk3", "-webgpu"]);
});

test("cheats run AFTER the map, cvars BEFORE it", () => {
  // [rc4l] Two opposite ordering requirements on one command line, and getting either backwards
  // fails silently. A cvar after +map does nothing to the level already standing; a cheat before it
  // acts on a player pawn that does not exist yet.
  const args = engineArgs({ map: "MAP01", cvars: { sv_nomonsters: 1 }, postMap: ["god"] }, "x.ini");
  const at = args.indexOf("+map");

  assert.ok(args.indexOf("sv_nomonsters") < at, "a cvar after +map never reaches the level");
  assert.ok(args.indexOf("+god") > at, "a cheat before +map has no pawn to act on");
});

test("true/false and 1/0 are the same answer, because Doom uses both", () => {
  // [rc4l] The very first run of the world check reported sv_nofreeaim as BAD: the expectation said
  // "false", the engine said "0", and the setting was correct all along. A check that cries wolf
  // over its own formatting gets ignored, which costs more than not having it.
  assert.ok(sameCvarValue("0", "false"));
  assert.ok(sameCvarValue("true", "1"));
  assert.ok(sameCvarValue("2", "2"));
  assert.ok(!sameCvarValue("1", "2"), "a genuinely different value must still fail");
  assert.ok(!sameCvarValue(null, "0"), "silence is not agreement");
});

test("a launch that fails after the engine started stops the engine and rethrows", async () => {
  const { stoppingOnFailure } = await import("../src/play.mjs");
  const stopped = [];
  const stop = async (inst) => { stopped.push(inst.port); };
  await assert.rejects(stoppingOnFailure({ port: 7796 }, async () => { throw new Error("no playable level after 90s"); }, stop), /no playable level/);
  assert.deepEqual(stopped, [7796]);
  assert.equal(await stoppingOnFailure({ port: 7797 }, async (inst) => inst.port, stop), 7797);
  assert.deepEqual(stopped, [7796]);
});

// [rc4l] A stand-in engine with a window: vid_setsize and the scale cvar change the render size input.windowpoint reports, as the engine does a frame later.
function viewEngine({ width, height, scale = 1, fov = 90, snaps = false }) {
  const st = { width, height, scale, fov, asked: [] };
  const c = { rpc: async (cmd) => { assert.equal(cmd, "input.windowpoint"); return { x: 0, y: 0, width: st.width, height: st.height }; } };
  const exec = async (_c, text) => {
    st.asked.push(text);
    const [name, ...a] = text.split(" ");
    if (name === "vid_scalefactor" && a.length) st.scale = Number(a[0]);
    else if (name === "fov" && a.length) st.fov = Number(a[0]);
    else if (name === "vid_setsize" && !snaps) { st.width = Math.round(Number(a[0]) * st.scale); st.height = Math.round(Number(a[1]) * st.scale); }
    else if (name === "vid_scalefactor") return `"vid_scalefactor" is "${st.scale}"\n`;
    else if (name === "fov") return `"fov" is "${st.fov}"\n`;
    return "";
  };
  return { c, cap: { exec, sleep: async () => {} }, st };
}

test("a view is read as its render size, scale and FOV", async () => {
  const { c, cap } = viewEngine({ width: 900, height: 900, scale: 0.5, fov: 110 });
  assert.deepEqual(await readView(c, cap), { width: 900, height: 900, scale: 0.5, fov: 110 });
  const silent = { exec: async () => "" };
  assert.deepEqual(await readView(c, silent), { width: 900, height: 900, scale: null, fov: null });
});

test("a view's window is its render size over its scale", () => {
  assert.deepEqual(windowForView({ width: 900, height: 900, scale: 0.5 }), { w: 1800, h: 1800 });
  assert.deepEqual(windowForView({ width: 1280, height: 720 }), { w: 1280, h: 720 });
  assert.deepEqual(windowForView({ width: 1280, height: 720, scale: 0 }), { w: 1280, h: 720 });
});

test("a view read back matches to the pixel a scale rounds by, and on the scale and FOV it names", () => {
  const want = { width: 900, height: 900, scale: 1, fov: 90 };
  assert.ok(sameView(want, { width: 901, height: 899, scale: 1, fov: 90 }));
  assert.ok(!sameView(want, { width: 1280, height: 960, scale: 1, fov: 90 }));
  assert.ok(!sameView(want, { width: 900, height: 900, scale: 0.5, fov: 90 }));
  assert.ok(!sameView(want, { width: 900, height: 900, scale: 1, fov: 110 }));
  assert.ok(sameView({ width: 900, height: 900 }, { width: 900, height: 900, scale: 0.5, fov: 110 }), "a bare size checks the size alone");
});

// [rc4l] 900x900 in the ini came up 1280x960, a mode the engine knew, and a square-only report could not be replayed.
test("a launch's snapped size is set to the view asked for and read back", async () => {
  const { c, cap, st } = viewEngine({ width: 1280, height: 960 });
  const r = await fitView(c, { width: 900, height: 900, scale: 0.5, fov: 100 }, cap);
  assert.equal(r.ok, true);
  assert.deepEqual(r.got, { width: 900, height: 900, scale: 0.5, fov: 100 });
  assert.ok(st.asked.includes("vid_setsize 1800 1800"));
});

test("a view already showing is left alone, and one that will not take is reported", async () => {
  const same = viewEngine({ width: 1280, height: 720 });
  assert.equal((await fitView(same.c, { width: 1280, height: 720 }, same.cap)).ok, true);
  assert.ok(!same.st.asked.some((t) => t.startsWith("vid_setsize")));
  const stuck = viewEngine({ width: 1280, height: 960, snaps: true });
  const r = await fitView(stuck.c, { width: 900, height: 900 }, stuck.cap, { tries: 2 });
  assert.equal(r.ok, false);
  assert.deepEqual([r.got.width, r.got.height], [1280, 960]);
});

// [rc4l] A scenario's 1280x720 window at render scale 0.5 renders 640x360, which a size compared as a render size never matched.
test("a --res window is the view it renders at its scale, and a view read once is not read again", async () => {
  assert.deepEqual(viewForWindow(1280, 720, 0.5), { width: 640, height: 360 });
  assert.deepEqual(viewForWindow(900, 900, null), { width: 900, height: 900 });
  const { c, cap, st } = viewEngine({ width: 640, height: 360, scale: 0.5 });
  const now = { width: 640, height: 360, scale: 0.5, fov: 90 };
  const r = await fitView(c, viewForWindow(1280, 720, now.scale), cap, { now });
  assert.equal(r.ok, true);
  assert.deepEqual(st.asked, [], "nothing asked of an engine already at the view");
});
