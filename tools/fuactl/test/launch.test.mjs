import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { spawnSync } from "node:child_process";
import os from "node:os";
import path from "node:path";
import { resolveEngine, engineArgs, describeExit, countEngineLines, missingCoreData, resFlag } from "../src/launch.mjs";
import { sweepDirFor, pickCmake, resolveTarget, readSession, sessionFileFor, restartSessionFile, cameraFromFlags, cvarValueFrom, parseCvarFlag, spotLaunch, parkFileFor, parkedLaunch, PARKED, clearPortSessions, liveTarget, pidAlive, buildErrors, summarizeGtest, sourcesStamp, standAt, launchFileFor, holdingLaunchFile, launchInProgress, awaitLaunch, atFromCamera, releaseOf, stepTag, sameFrameCommand, stageCompanions } from "../src/shot.mjs";

// Run `fn` with cwd moved to `dir` and FUACTL_ENGINE cleared, then put both back.
function inDir(dir, fn) {
  const cwd = process.cwd();
  const env = process.env.FUACTL_ENGINE;
  delete process.env.FUACTL_ENGINE;
  process.chdir(dir);
  try {
    return fn();
  } finally {
    process.chdir(cwd);
    if (env !== undefined) process.env.FUACTL_ENGINE = env;
  }
}

// A repo root with one staged binary at `rel`, plus the tools/fuactl folder a command may run from.
function repoWith(rel) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-resolve-")));
  const bin = path.join(root, rel);
  fs.mkdirSync(path.dirname(bin), { recursive: true });
  fs.writeFileSync(bin, "");
  fs.mkdirSync(path.join(root, "tools", "fuactl"), { recursive: true });
  return { root, bin };
}

// [rc4l] The list held only the mac .app, so on Windows every command needing the binary died with
// "engine binary not found" -- including `ui screenshot`, which only wants the folder the PNG lands
// in. Swept over all three so a platform cannot be added to the compile scripts and missed here.
for (const rel of [
  "build/ForkUnderA.app/Contents/MacOS/forkundera",
  "dist-windows/forkundera.exe",
  "dist-linux/forkundera",
]) {
  test(`resolveEngine finds a binary staged at ${rel}`, () => {
    const { root, bin } = repoWith(rel);

    assert.equal(inDir(root, resolveEngine), bin, "from the repo root");
    assert.equal(inDir(path.join(root, "tools", "fuactl"), resolveEngine), bin, "from tools/fuactl");
  });
}

test("resolveEngine prefers FUACTL_ENGINE over anything staged", () => {
  const { root, bin } = repoWith("dist-windows/forkundera.exe");
  const override = path.join(root, "elsewhere", "forkundera");
  fs.mkdirSync(path.dirname(override), { recursive: true });
  fs.writeFileSync(override, "");

  const cwd = process.cwd();
  const env = process.env.FUACTL_ENGINE;
  process.env.FUACTL_ENGINE = override;
  process.chdir(root);
  try {
    assert.equal(resolveEngine(), override);
    assert.notEqual(resolveEngine(), bin);
  } finally {
    process.chdir(cwd);
    if (env === undefined) delete process.env.FUACTL_ENGINE; else process.env.FUACTL_ENGINE = env;
  }
});

// ---------------------------------------------------------------- the command line

// Read the value `+set <name> V` sets, or undefined when the cvar is never set.
function setValue(args, name) {
  for (let i = 0; i + 2 < args.length; i++) {
    if (args[i] === "+set" && args[i + 1] === name) return args[i + 2];
  }
  return undefined;
}

test("a harness instance keeps the OS mouse out of the game", () => {
  // use_mouse is the one that stops a stray cursor: on Windows it gates OS mouse buttons, movement
  // and wheel out of the GUI (i_input.cpp), and in a live level it stops the view being turned.
  const args = engineArgs({}, "x.ini");

  assert.equal(setValue(args, "use_mouse"), "0");
  assert.equal(setValue(args, "use_joystick"), "0");
});

test("a harness instance does NOT disable the menu mouse it injects into", () => {
  // [rc4l] m_use_mouse used to be forced to 0 here. menu.cpp drops every GUI mouse event when it is
  // 0, injected ones included, so `ui click` reported {"clicked":[x,y]} and did nothing -- on every
  // platform. Setting it at all is the bug; the physical cursor is use_mouse's job, above.
  assert.equal(setValue(engineArgs({}, "x.ini"), "m_use_mouse"), undefined);
});

test("every instance gets its own config, never the user's ini", () => {
  // Without -config the engine reads the user's real ini and writes its overrides back on exit,
  // which both poisons a clean baseline and breaks the mouse of whoever owns the machine.
  const args = engineArgs({}, "/tmp/run-7/fua-bridge.ini");
  const at = args.indexOf("-config");

  assert.notEqual(at, -1, "-config is not optional");
  assert.equal(args[at + 1], "/tmp/run-7/fua-bridge.ini");
});

test("seed and map reach the command line, and a client connects instead of loading a map", () => {
  const local = engineArgs({ seed: 777, map: "MAP07", skill: 4 }, "x.ini");
  assert.deepEqual(local.slice(local.indexOf("-rngseed"), local.indexOf("-rngseed") + 2), ["-rngseed", "777"]);
  assert.equal(local[local.indexOf("+map") + 1], "MAP07");
  assert.equal(local[local.indexOf("-skill") + 1], "4");

  const client = engineArgs({ connect: "127.0.0.1:10666" }, "x.ini");
  assert.equal(client[client.indexOf("+connect") + 1], "127.0.0.1:10666");
  assert.equal(client.includes("+map"), false, "a joining client must not also load a local map");
});

test("cvars are set BEFORE the map loads, not after it", () => {
  // [rc4l] The engine runs `+` arguments in the order given, and a level-start cvar set after +map
  // does nothing to the level already standing. sv_nomonsters is the one that bites: an instance
  // asked for no monsters came up full of them, and they shove the player off the position a repro
  // was recorded at and shoot whatever is being measured.
  //
  // A comment above the cvar loop claimed this ordering for a long time while the code did the
  // opposite, so it is asserted here rather than described there.
  const args = engineArgs({ map: "MAP07", cvars: { sv_nomonsters: 1, fua_vulkan: 1 } }, "x.ini");

  for (const key of ["sv_nomonsters", "fua_vulkan"]) {
    const at = args.indexOf(key);
    assert.notEqual(at, -1, `${key} never reached the command line`);
    assert.ok(at < args.indexOf("+map"), `${key} is set after +map, so the level never sees it`);
  }
});

test("resolveEngine names every path it tried when it finds nothing", () => {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-resolve-")));

  assert.throws(() => inDir(root, resolveEngine), (err) => {
    // The message is the whole fix for a mis-set path, so it has to list the real candidates.
    assert.match(err.message, /FUACTL_ENGINE/);
    assert.match(err.message, /dist-windows/);
    assert.match(err.message, /dist-linux/);
    assert.match(err.message, /ForkUnderA\.app/);
    return true;
  });
});

test("an exit code the engine dies with is named, signed or not", () => {
  // [rc4l] 3221225781 is what a run from build-win/ printed when its DLLs were not beside it.
  assert.match(describeExit(3221225781), /0xc0000135: a DLL next to the binary is missing/);
  assert.match(describeExit(-1073741819), /0xc0000005: access violation/);
  assert.equal(describeExit(1), "1");
  assert.equal(describeExit(null), "still running");
});

test("engines are counted from tasklist's lines, and its no-match message counts as none", () => {
  const two = "\r\nforkundera.exe    1234 Console    1  512,000 K\r\nforkundera.exe    5678 Console    1  498,000 K\r\n";
  assert.equal(countEngineLines(two), 2);
  assert.equal(countEngineLines("INFO: No tasks are running which match the specified criteria.\r\n"), 0);
});

test("engines are counted from ps names on macOS and Linux, and nothing merely like one counts", () => {
  const ps = "launchd\n/Applications/ForkUnderA.app/Contents/MacOS/forkundera\nforkundera\nforkundera-helper\nbash\n";
  assert.equal(countEngineLines(ps), 2);
});

test("a pair lands in sweep/ beside whichever engine is running", () => {
  assert.equal(sweepDirFor("/repo/dist-windows/forkundera.exe"), "/repo/dist-windows/sweep");
  assert.equal(sweepDirFor("/repo/build-win/Release/forkundera.exe"), "/repo/build-win/Release/sweep");
});

test("cmake comes from FUACTL_CMAKE, then any Visual Studio 2022 edition, then PATH", () => {
  const pf = "/pf";
  const pro = path.join(pf, "Microsoft Visual Studio/2022", "Professional", "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
  assert.equal(pickCmake({ FUACTL_CMAKE: "/x/cmake", ProgramFiles: pf }, () => true), "/x/cmake");
  assert.equal(pickCmake({ ProgramFiles: pf }, (p) => p === pro), pro);
  assert.equal(pickCmake({}, () => false), "cmake");
});

test("a command means --port first, then the last play session, and nothing when there is neither", () => {
  const s = { port: 7801, token: "s", engine: "/b/forkundera.exe", pid: 42 };
  assert.deepEqual(resolveTarget({ port: "7799", token: "t" }, s), { port: 7799, token: "t", engine: null, pid: null });
  assert.deepEqual(resolveTarget({}, s), { port: 7801, token: "s", engine: "/b/forkundera.exe", pid: 42 });
  assert.deepEqual(resolveTarget({ token: "t" }, { port: 7801, token: "s" }), { port: 7801, token: "t", engine: null, pid: null });
  assert.equal(resolveTarget({}, null), null);
  assert.deepEqual(resolveTarget({ port: "7801" }, s), { port: 7801, token: "s", engine: "/b/forkundera.exe", pid: 42 });
  assert.deepEqual(resolveTarget({ port: "7801", token: "t" }, s).pid, null, "a token given by hand is not the session's engine");
  assert.deepEqual(resolveTarget({ token: "t" }, s).pid, null);
  assert.deepEqual(resolveTarget({ port: "7799" }, s), { port: 7799, token: null, engine: null, pid: null });
  assert.equal(sessionFileFor("/x/.play-session", 7801), "/x/.play-session-7801");
  // [rc4l] play --restart --port 7801 replaces the instance on 7801, never the last launch on another port.
  assert.equal(restartSessionFile("/x/.play-session", "7801"), "/x/.play-session-7801");
  assert.equal(restartSessionFile("/x/.play-session", undefined), "/x/.play-session");
});

// [rc4l] A build launched from build-win writes its captures beside itself, so a shot that looked beside the staged binary found nothing.
test("a play session records the binary it launched and its engine's process", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "session-"));
  const file = path.join(dir, ".play-session");
  fs.writeFileSync(file, "PORT=7799\nTOKEN=abc\nPID=4242\nENGINE=F:/A B/build-win/Release/forkundera.exe\n");
  assert.deepEqual(readSession(file), { port: 7799, token: "abc", engine: "F:/A B/build-win/Release/forkundera.exe", pid: 4242 });
  fs.writeFileSync(file, "PORT=7799\nTOKEN=abc\n");
  assert.deepEqual(readSession(file), { port: 7799, token: "abc", engine: null, pid: null });
  assert.equal(readSession(path.join(dir, "absent")), null);
});

// [rc4l] ready connected with a dead launch's token, or none, while play still checked the new engine's world, and the bridge's newest client knocked play off it.
test("ready connects only with a token whose engine is still running", () => {
  const alive = (pid) => pid === 7;
  assert.equal(liveTarget({ port: 7799, token: "t", pid: 7 }, alive), true);
  assert.equal(liveTarget({ port: 7799, token: "t", pid: 8 }, alive), false, "a stale session names a dead engine");
  assert.equal(liveTarget({ port: 7799, token: "t", pid: null }, alive), true, "no pid to check: --token given by hand");
  assert.equal(liveTarget({ port: 7799, token: null, pid: 7 }, alive), false);
  assert.equal(liveTarget(null, alive), false);
  assert.equal(pidAlive(process.pid), true);
  const gone = spawnSync(process.execPath, ["-e", ""]).pid;
  assert.equal(pidAlive(gone), false);
});

test("the live camera stands as the position a spot or standAt takes", () => {
  assert.deepEqual(atFromCamera({ x: 1, y: 2, z: 3, yaw: 90, pitch: -10, viewz: 44, map: "MAP01" }), { x: 1, y: 2, z: 3, angle: 90, pitch: -10 });
});

// [rc4l] Queue #38 showed only for two tics of a crouch, and a held action stepped tic by tic is how it was caught.
test("a held action is let go by its minus form, and each stepped tic's capture sorts in order", () => {
  assert.equal(releaseOf("+crouch"), "-crouch");
  assert.equal(releaseOf("+use"), "-use");
  assert.equal(stepTag("cs", 3), "cs_03");
  assert.equal(stepTag("cs", 12), "cs_12");
});

// [rc4l] Queue #39: the fan animates on the clock, so a GL frame and a Vulkan frame asked for apart showed different blades.
test("one console line captures both renderers in the same frame", () => {
  assert.equal(sameFrameCommand("fan", "F:/x/sweep/fan_vk.png"), "screenshot sweep/fan_gl; fua_diligent_shot F:/x/sweep/fan_vk.png");
});

test("a camera comes from --spot, else --at with --face, else nowhere", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "spots-"));
  const spots = path.join(dir, "spots.json");
  fs.writeFileSync(spots, JSON.stringify({ spots: { s: { x: 1, y: 2, z: 3, angle: 90, shows: "it" } } }));
  assert.deepEqual(cameraFromFlags({ spot: "s", at: "9,9,9" }, spots), { x: 1, y: 2, z: 3, angle: 90, pitch: 0, shows: "it" });
  assert.deepEqual(cameraFromFlags({ at: "4,5,6", face: "180,-30" }, spots), { x: 4, y: 5, z: 6, angle: 180, pitch: -30 });
  assert.deepEqual(cameraFromFlags({ at: "4,5,6" }, spots, "90,0"), { x: 4, y: 5, z: 6, angle: 90, pitch: 0 });
  assert.equal(cameraFromFlags({}, spots), null);
  assert.equal(cameraFromFlags({ spot: "s", force: true }, spots).force, 1);   // [rc4l] a spot saved where something now stands
  assert.equal(cameraFromFlags({ force: true }, spots), null);
});

test("a cvar's value is read off the console echo, and anything else is no value", () => {
  assert.equal(cvarValueFrom('"fua_mesh_sky" is "true"'), "true");
  assert.equal(cvarValueFrom('"gl_plane_reflection" is "0"\n'), "0");
  assert.equal(cvarValueFrom("Unknown command \"nope\""), null);
});

test("a binary folder with no fua_core pk3 is refused before the engine can open an error box", () => {
  // [rc4l] A comparison build copied without its pk3s popped "Cannot find fua_core_v0.3.0.pk3" on the user's screen at every launch.
  assert.match(missingCoreData("/x", ["forkundera.exe", "zmusic.dll"]), /no fua_core_\*\.pk3 beside \/x/);
  assert.equal(missingCoreData("/x", ["forkundera.exe", "fua_core_v0.3.0.pk3"]), null);
  assert.match(missingCoreData("/x", ["fua_core_.pk3"]), /no fua_core/);
});

test("--cvar reads as name=value pairs, a bare name meaning 1, and nothing when it is absent", () => {
  assert.deepEqual(parseCvarFlag("fua_dg_standalone=0, gl_lights=1,sv_cheats"), { fua_dg_standalone: "0", gl_lights: "1", sv_cheats: "1" });
  assert.equal(parseCvarFlag(undefined), undefined);
  assert.equal(parseCvarFlag(true), undefined);
  assert.deepEqual(parseCvarFlag("a=b=c,"), { a: "b=c" });
});

test("a spot launches its map with its wads found by name in the store, and names what is missing", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "spotlaunch-"));
  const spots = path.join(dir, "spots.json");
  fs.writeFileSync(spots, JSON.stringify({ spots: { s: { map: "WDI04", x: 0, y: 0, z: 0, angle: 0,
    loaded: { iwad: "doom2.wad", pwads: ["Whodunit_Fixed2.pk3 fc8a", "wdi_touchupv9.11.wad 2ccc", "gone.pk3"] } }, bare: { map: "MAP01" } } }));
  const r = spotLaunch(spots, "s", "/store", ["whodunit_fixed2.pk3", "WDI_TOUCHUPV9.11.WAD", "other.wad"]);
  assert.equal(r.map, "WDI04");
  assert.deepEqual(r.files, [path.join("/store", "whodunit_fixed2.pk3"), path.join("/store", "WDI_TOUCHUPV9.11.WAD")]);
  assert.deepEqual(r.missing, ["gone.pk3"]);
  assert.deepEqual(spotLaunch(spots, "bare", path.join(dir, "nowhere")), { map: "MAP01", files: [], missing: [] });
  assert.throws(() => spotLaunch(spots, "nope", "/store", []), /no such spot: nope/);
  fs.rmSync(dir, { recursive: true, force: true });
});

// [rc4l] Every exclusive scenario run meant saving the user's camera, quitting their window and relaunching it there by hand.
test("a parked instance relaunches at its camera with its wads, side by side when its Vulkan window was un-embedded", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "park-"));
  const park = parkFileFor(path.join(dir, ".play-session"), 7801);
  assert.equal(park, path.join(dir, ".play-session-7801.park"));
  const record = (embed) => ({ spots: { [PARKED]: { map: "WDI18", x: -656, y: -480, z: 0, angle: 296, pitch: -3.6,
    loaded: { iwad: "doom2.wad", pwads: ["whodunit_fixed2.pk3 fc8a"] }, renderer: embed === undefined ? {} : { fua_dg_embed: embed } } } });
  fs.writeFileSync(park, JSON.stringify(record("false")));
  const r = parkedLaunch(park, "/store", ["whodunit_fixed2.pk3"]);
  assert.deepEqual([r.map, r.files, r.missing, r.sideBySide], ["WDI18", [path.join("/store", "whodunit_fixed2.pk3")], [], true]);
  assert.deepEqual([r.at.x, r.at.y, r.at.angle, r.at.pitch], [-656, -480, 296, -3.6]);
  fs.writeFileSync(park, JSON.stringify(record("0")));
  assert.equal(parkedLaunch(park, "/store", []).sideBySide, true);
  fs.writeFileSync(park, JSON.stringify(record("true")));
  assert.equal(parkedLaunch(park, "/store", []).sideBySide, false);
  fs.writeFileSync(park, JSON.stringify(record(undefined)));
  assert.equal(parkedLaunch(park, "/store", []).sideBySide, false);
  assert.equal(parkedLaunch(park, "/store", []).noGl, false);
  // [rc4l] A -vulkan launch comes back without GL, since dumprenderer's backend was filed with it.
  const vk = record("true");
  vk.spots[PARKED].renderer.backend = "vulkan";
  fs.writeFileSync(park, JSON.stringify(vk));
  assert.equal(parkedLaunch(park, "/store", []).noGl, true);
  fs.rmSync(dir, { recursive: true, force: true });
});

// [rc4l] `ready --port 7799` read the last launch's token there and waited its whole timeout on a dead one.
test("a launch clears the session files that name its port and leaves another port's alone", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "sessions-"));
  const shared = path.join(dir, ".play-session");
  fs.writeFileSync(shared, "PORT=7799\nTOKEN=old\n");
  fs.writeFileSync(sessionFileFor(shared, 7799), "PORT=7799\nTOKEN=old\n");
  fs.writeFileSync(sessionFileFor(shared, 7801), "PORT=7801\nTOKEN=theirs\n");
  assert.deepEqual(clearPortSessions(shared, 7799), [shared, sessionFileFor(shared, 7799)]);
  assert.equal(readSession(sessionFileFor(shared, 7801)).token, "theirs");
  fs.writeFileSync(shared, "PORT=7801\nTOKEN=theirs\n");
  assert.deepEqual(clearPortSessions(shared, 7799), [], "the shared file names another port, and 7799's is gone already");
  assert.equal(readSession(shared).port, 7801);
  fs.rmSync(dir, { recursive: true, force: true });
});

// [rc4l] A `rpc sim.tic` poll started beside `play` took the single-client bridge mid-handshake, four times in one session, and the launch died with "hello timeout".
test("a command waits while a launch on its port checks its world, and not for a dead or finished one", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "launching-"));
  const shared = path.join(dir, ".play-session");
  const file = launchFileFor(shared, 7799);
  const alive = (pid) => pid === 7;
  let during = null;
  const r = await holdingLaunchFile(shared, 7799, async () => { during = launchInProgress(file, alive); return "up"; }, 7);
  assert.equal(r, "up");
  assert.equal(during, true);
  assert.equal(fs.existsSync(file), false, "released once the launch is up");
  await assert.rejects(holdingLaunchFile(shared, 7799, async () => { throw new Error("no playable level"); }, 7), /no playable level/);
  assert.equal(fs.existsSync(file), false, "released when the launch fails too");
  fs.writeFileSync(file, "PORT=7799\nPID=8\n");
  assert.equal(launchInProgress(file, alive), false, "a killed launch's file names a dead process");
  assert.equal(await awaitLaunch(shared, 7799, { isAlive: alive }), false);
  fs.writeFileSync(file, "PORT=7799\nPID=7\n");
  setTimeout(() => fs.rmSync(file, { force: true }), 60);
  assert.equal(await awaitLaunch(shared, 7799, { isAlive: alive, pollMs: 10 }), true);
  fs.writeFileSync(file, "PORT=7799\nPID=7\n");
  assert.equal(await awaitLaunch(shared, 7799, { isAlive: alive, pollMs: 10, timeoutMs: 30 }), true, "bounded by its timeout");
  assert.equal(launchInProgress(path.join(dir, "absent"), alive), false);
  fs.rmSync(dir, { recursive: true, force: true });
});

// [rc4l] A build run beside a running engine failed to link with nothing saying why.
test("a build's errors are read off its log, and a binary held open is named as that", () => {
  const out = "a.cpp(3): error C2065: x\nb.cpp: warning C4996: y\nLINK : fatal error LNK1104: cannot open file 'forkundera.exe'\n";
  assert.deepEqual(buildErrors(out), { errors: ["a.cpp(3): error C2065: x", "LINK : fatal error LNK1104: cannot open file 'forkundera.exe'"], locked: true });
  assert.deepEqual(buildErrors("ok\n"), { errors: [], locked: false });
});

test("a unit test run reports how many passed and which failed, each once", () => {
  const out = "[==========] 3 tests ran.\n[  PASSED  ] 2 tests.\n[  FAILED  ] 1 test, listed below:\n[  FAILED  ] Suite.Case\n[  FAILED  ] Suite.Case (0 ms)\n";
  assert.deepEqual(summarizeGtest(out), { passed: 2, failed: ["Suite.Case"] });
  assert.deepEqual(summarizeGtest("nothing"), { passed: 0, failed: [] });
});

// [rc4l] play --spot at WDI18's #29 spot died on an occupied spot and left no instance at all.
test("a spot refused as occupied is stood on anyway, and any other refusal still fails", async () => {
  const fake = (refuse) => {
    const calls = [];
    return { calls, rpc: async (method, args) => { calls.push(args); if (refuse(args)) throw new Error(refuse(args)); return {}; } };
  };
  const at = { x: 1, y: 2, z: 3, angle: 90 };
  const ok = fake(() => null);
  await standAt(ok, at, () => {});
  assert.deepEqual(ok.calls, [{ x: 1, y: 2, z: 3, angle: 90, pitch: 0 }]);
  const said = [];
  const busy = fake((a) => (a.force ? null : "that spot is solid or occupied -- pass force:1 to go there anyway"));
  await standAt(busy, at, (m) => said.push(m));
  assert.equal(busy.calls.length, 2);
  assert.equal(busy.calls[1].force, 1);
  assert.equal(said.length, 1);
  const broken = fake(() => "no such map");
  await assert.rejects(standAt(broken, at, () => {}), /no such map/);
  const forced = fake(() => "that spot is solid or occupied");
  await assert.rejects(standAt(forced, { ...at, force: 1 }, () => {}), /occupied/);
  assert.equal(forced.calls.length, 1);
});

// [rc4l] A new *_compute.cpp linked only on the second build, since the glob is read at configure time.
test("the globbed sources' stamp ignores order and path separators, and changes when a file appears", () => {
  const a = sourcesStamp(["features/b/x_compute.cpp", "features\\a\\y_compute.cpp"]);
  assert.equal(a, sourcesStamp(["features/a/y_compute.cpp", "features/b/x_compute.cpp"]));
  assert.notEqual(a, sourcesStamp(["features/a/y_compute.cpp", "features/b/x_compute.cpp", "features/c/z_compute.cpp"]));
});

// [rc4l] play, fps and the sweep's --size each split WxH by hand until they shared this.
test("a --res flag is a width and height, and an absent one leaves both to the launch default", () => {
  assert.deepEqual(resFlag("1280x800"), { width: 1280, height: 800 });
  assert.deepEqual(resFlag(undefined), { width: undefined, height: undefined });
  assert.deepEqual(resFlag(true), { width: undefined, height: undefined });
});

test("a launch draws with GL unless it asks for -vulkan, since the engine's default is Vulkan alone", () => {
  assert.ok(engineArgs({}, "x.ini").includes("-gl"));
  const vk = engineArgs({ extraArgs: ["-vulkan"] }, "x.ini");
  assert.ok(vk.includes("-vulkan"));
  assert.ok(!vk.includes("-gl"));
  assert.ok(!engineArgs({ engineDefault: true }, "x.ini").includes("-gl"));
});

// [rc4l] The staged binary's symbols went stale while its exe moved on, so samples and crashes of it named no function.
test("the pk3s and the symbols are staged beside the exe, and nothing else from the build", () => {
  assert.deepEqual(stageCompanions(["forkundera.exe", "forkundera.pdb", "fua_core.pk3", "brightmaps.pk3", "zdoom.ilk", "other.pdb"]),
    ["forkundera.pdb", "fua_core.pk3", "brightmaps.pk3"]);
});
