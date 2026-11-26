import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { uzdoomExe, uzdoomCommandLine, logFileIn, runUzdoom, newestPng, uzdoomScreenshot, uzdoomEntered, LOG_NAME, UZDOOM_BACKENDS, parityApis, parityUzdoomBackend, ourApiFault, OUR_API_NAMES } from "../src/uzdoom.mjs";

test("the UZDoom to run is FUACTL_UZDOOM, else the Release build beside the repository", () => {
  assert.equal(uzdoomExe({ FUACTL_UZDOOM: "X:/u/uzdoom.exe" }), "X:/u/uzdoom.exe");
  assert.match(uzdoomExe({}), /UZDoom-build[\\/]Release[\\/]uzdoom\.exe$/);
});

test("the log is a bare name, since UZDoom writes log-<name>.txt in its working directory and fails on a path", () => {
  const line = uzdoomCommandLine({ exe: "u.exe", iwad: "f.wad", pk3: "p.pk3", map: "MAP02", chain: "wait 35;puke 1;quit", dir: "D", extra: ["+crosshair", 0] });
  assert.match(line, /\+logfile uzdoom \+crosshair 0 \+wait 1;map MAP02;/);
  assert.equal(LOG_NAME, "uzdoom");
  assert.equal(logFileIn("D", () => false), path.join("D", "log-uzdoom.txt"));
  // [Doruk-Aksoy] A release writes the bare name, which is the log only where master's is absent.
  assert.equal(logFileIn("D", (f) => f === path.join("D", "uzdoom")), path.join("D", "uzdoom"));
  assert.equal(logFileIn("D", () => true), path.join("D", "log-uzdoom.txt"));
  assert.match(line, /^"u\.exe" -iwad "f\.wad" -file "p\.pk3" -config "D[\\/]uzdoom\.ini" -nosound -nomusic/);
  assert.match(line, /\+wait 1;map MAP02;wait 35;puke 1;quit$/);
  assert.match(line, / \+i_pauseinbackground 0 \+vid_activeinbackground 1 /);
  assert.match(line, / \+vid_preferbackend 1 /, "Vulkan by default");
  assert.doesNotMatch(uzdoomCommandLine({ exe: "u.exe", map: "MAP01", chain: "quit", dir: "D" }), /-file/);
});

// [Doruk-Aksoy] A fake UZDoom: a folder with an exe and a launcher that logs, and screenshots, only on the run `on`.
function fake(on, png = true, entered = true) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "uzdoom-test-"));
  const exe = path.join(dir, "uzdoom.exe");
  fs.writeFileSync(exe, "");
  const calls = [];
  const run = (cmd, args, opts) => {
    calls.push(opts.env.FUACTL_LAUNCH_CMDLINE);
    const at = args[args.indexOf("-Dir") + 1];
    if (calls.length === on) {
      fs.writeFileSync(logFileIn(at), `${entered ? (/;map (\S+?);/.exec(opts.env.FUACTL_LAUNCH_CMDLINE) || [])[1] + " - Test\n" : ""}parity 1\n`);
      if (png) fs.writeFileSync(path.join(at, "shot.png"), "");
    }
  };
  return { dir, exe, calls, run };
}

test("a run reads the log UZDoom wrote, tries again when it wrote none, and needs a UZDoom", () => {
  const f = fake(2);
  assert.equal(runUzdoom({ exe: f.exe, map: "MAP01", chain: "quit", dir: f.dir, run: f.run }), "MAP01 - Test\nparity 1\n");
  assert.equal(f.calls.length, 2);
  assert.match(f.calls[1], /-config "[^"]*try2[\\/]uzdoom\.ini"/);
  assert.equal(runUzdoom({ exe: f.exe, map: "MAP01", chain: "quit", dir: path.join(f.dir, "none"), run: () => {}, tries: 1 }), "");
  assert.throws(() => runUzdoom({ exe: path.join(f.dir, "missing.exe"), map: "MAP01", chain: "quit", dir: f.dir }), /no UZDoom at .*FUACTL_UZDOOM/);
});

// [Doruk-Aksoy] UZDoom runs on the backend asked for, by vid_preferbackend's value, and refuses one it does not have.
test("UZDoom runs on Vulkan or OpenGL as asked", () => {
  assert.deepEqual(UZDOOM_BACKENDS, { gl: 0, vulkan: 1 });
  assert.match(uzdoomCommandLine({ exe: "u.exe", map: "MAP01", chain: "quit", dir: "D", backend: "gl" }), / \+vid_preferbackend 0 /);
  assert.throws(() => uzdoomCommandLine({ exe: "u.exe", map: "MAP01", chain: "quit", dir: "D", backend: "metal" }), /no UZDoom backend metal/);
  const f = fake(1);
  uzdoomScreenshot({ exe: f.exe, map: "MAP01", dir: f.dir, run: f.run, backend: "gl", blank: () => false });
  assert.match(f.calls[0], / \+vid_preferbackend 0 /);
});

// [Doruk-Aksoy] A harness runs ours on both APIs unless told one, and UZDoom on OpenGL unless told Vulkan.
test("a parity harness runs ours on Vulkan and OpenGL against UZDoom's OpenGL by default", () => {
  assert.deepEqual(parityApis([]), ["vulkan", "opengl"]);
  assert.deepEqual(parityApis(["--api", "opengl"]), ["opengl"]);
  assert.deepEqual(parityApis(["--api", "vulkan"]), ["vulkan"]);
  assert.deepEqual(parityApis(["--api", "both"]), ["vulkan", "opengl"]);
  assert.throws(() => parityApis(["--api", "webgpu"]), /--api takes vulkan, opengl or both/);
  assert.equal(parityUzdoomBackend([]), "gl");
  assert.equal(parityUzdoomBackend(["--uzdoom-backend", "vulkan"]), "vulkan");
  assert.equal(parityUzdoomBackend(["--uzdoom-backend", "gl"]), "gl");
  assert.throws(() => parityUzdoomBackend(["--uzdoom-backend", "d3d"]), /--uzdoom-backend takes gl or vulkan/);
});

// [Doruk-Aksoy] A blank picture is an error naming the backend, since a black UZDoom shot would match a black one of ours.
test("a blank UZDoom picture is an error", () => {
  const f = fake(1);
  assert.throws(() => uzdoomScreenshot({ exe: f.exe, map: "MAP01", dir: f.dir, run: f.run, backend: "gl", blank: () => true }), /UZDoom's gl picture of MAP01 is blank/);
});

test("a screenshot is the newest picture the run left, without the crosshair or weapon, or an error with the log's end", () => {
  const f = fake(1);
  const png = uzdoomScreenshot({ exe: f.exe, pk3: "p.pk3", map: "MAP03", dir: f.dir, run: f.run, blank: () => false });
  assert.equal(path.basename(png), "shot.png");
  assert.match(f.calls[0], /\+crosshair 0 \+r_drawplayersprites 0 \+wait 1;map MAP03;wait 70;screenshot;wait 35;quit$/);
  const none = fake(1, false);
  assert.throws(() => uzdoomScreenshot({ exe: none.exe, map: "MAP03", dir: none.dir, run: none.run }), /no screenshot of MAP03[\s\S]*parity 1/);
  assert.equal(newestPng(fs.mkdtempSync(path.join(os.tmpdir(), "uzdoom-empty-"))), null);
});

// [Doruk-Aksoy] A leading +map is a startup warp that leaves UZDoom's OpenGL black, so on every backend the chain loads the map a tic in, and no run takes focus.
test("the chain loads the map a tic in on either backend, and no run takes focus", () => {
  for (const backend of Object.keys(UZDOOM_BACKENDS)) {
    const line = uzdoomCommandLine({ exe: "u.exe", map: "MAP04", chain: "wait 70;screenshot;quit", dir: "D", backend });
    assert.match(line, / \+wait 1;map MAP04;wait 70;screenshot;quit$/);
    assert.doesNotMatch(line, /\+map/);
  }
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "uzquiet-"));
  let args = null;
  runUzdoom({ exe: process.execPath, dir, chain: "quit", backend: "gl", tries: 1, run: (cmd, a) => { args = a; return { status: 0 }; } });
  assert.deepEqual(args.slice(-6), ["-Exe", process.execPath, "-Dir", dir, "-TimeoutMs", "60000"]);
});

// [Doruk-Aksoy] A title screen is not blank, so a run must log entering its map before its picture counts.
test("a picture counts only when UZDoom logged entering its map", () => {
  assert.equal(uzdoomEntered("Init complete.\nMAP21 - Unnamed\n", "MAP21"), true);
  assert.equal(uzdoomEntered("map21 - x", "MAP21"), true);
  assert.equal(uzdoomEntered("Can't find map MAP01;wait\nCaptured Screenshot.png\n", "MAP01"), false);
  assert.equal(uzdoomEntered("MAP210 - other\n", "MAP21"), false);
  const f = fake(1, true, false);
  assert.throws(() => uzdoomScreenshot({ exe: f.exe, map: "MAP05", dir: f.dir, run: f.run, blank: () => false }), /UZDoom never entered MAP05/);
});

// [Doruk-Aksoy] A build without the OpenGL device runs Vulkan on -opengl, so a harness must refuse its picture rather than call it OpenGL's.
test("our launch counts only on the API it asked for", () => {
  assert.deepEqual(OUR_API_NAMES, { vulkan: "Vulkan", opengl: "OpenGL" });
  assert.equal(ourApiFault("opengl", "renderer=vulkan backend_api=OpenGL"), null);
  assert.equal(ourApiFault("vulkan", "renderer=vulkan backend_api=Vulkan"), null);
  assert.match(ourApiFault("opengl", "renderer=vulkan backend_api=Vulkan"), /asked for opengl and runs Vulkan; does this build have the opengl device/);
  assert.match(ourApiFault("vulkan", "renderer=opengl"), /runs opengl/);
  assert.match(ourApiFault("vulkan", ""), /runs no renderer/);
});
