// [rc4l] Matched GL/Vulkan pairs, and the build that produces them.
//
// The comparison that matters is the SAME CAMERA in both renderers. Doing it by hand meant a dozen
// ordered calls every time, which is how a stale binary and a wrong-camera comparison both slipped
// through -- and each of those cost a diagnosis, because a pair taken at two cameras disagrees
// everywhere and looks like a renderer that has gone completely wrong.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import * as ui from "./ui.mjs";
import { exec, waitTics } from "./capture.mjs";
import { engineProcessCount } from "./launch.mjs";
import { decodePNG, isBlankImage, boxMean } from "./pngcore.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] Find the running hands-on instance without depending on a pipe still existing.
//
// stdout is whatever the caller redirected it into, and a play session that has to be found again
// -- to capture the thing just walked to -- must not depend on that.
// [rc4l] Each launch's own session file beside the shared one, so two instances do not overwrite each other's token.
export function sessionFileFor(sessionFile, port) { return `${sessionFile}-${port}`; }

export function readSession(file = ".play-session") {
  if (!fs.existsSync(file)) return null;
  const text = fs.readFileSync(file, "utf8");
  const port = /PORT=(\d+)/.exec(text), token = /TOKEN=(\w+)/.exec(text), engine = /ENGINE=(.+)/.exec(text), pid = /PID=(\d+)/.exec(text);
  return port ? { port: Number(port[1]), token: token ? token[1] : null, engine: engine ? engine[1].trim() : null,
    pid: pid ? Number(pid[1]) : null } : null;
}

// A named camera from spots.json. A fault found by looking at it is worth a name, not four numbers
// pasted out of a screenshot.
export function readSpot(spotsPath, name) {
  const db = JSON.parse(fs.readFileSync(spotsPath, "utf8"));
  const s = db.spots[name];
  if (!s) throw new Error(`no such spot: ${name}`);
  return { x: s.x, y: s.y, z: s.z, angle: s.angle, pitch: s.pitch || 0, shows: s.shows };
}

// [rc4l] --cvar's "name=value,name2=value2" as an object, a bare name meaning 1; undefined when the flag is absent.
export function parseCvarFlag(text) {
  if (text == null || text === true) return undefined;
  return Object.fromEntries(String(text).split(",").filter((kv) => kv.trim()).map((kv) => {
    const i = kv.indexOf("=");
    return i < 0 ? [kv.trim(), "1"] : [kv.slice(0, i).trim(), kv.slice(i + 1).trim()];
  }));
}

// [rc4l] The map and wads a spot was recorded with, its wads found by name (any case) in storeDir: files in load order, and the names it could not find.
export function spotLaunch(spotsPath, name, storeDir, listing = fs.existsSync(storeDir) ? fs.readdirSync(storeDir) : []) {
  const db = JSON.parse(fs.readFileSync(spotsPath, "utf8"));
  const s = db.spots[name];
  if (!s) throw new Error(`no such spot: ${name}`);
  const byName = new Map(listing.map((f) => [f.toLowerCase(), f]));
  const files = [], missing = [];
  for (const entry of (s.loaded && s.loaded.pwads) || []) {
    const wad = entry.split(" ")[0];
    const hit = byName.get(wad.toLowerCase());
    if (hit) files.push(path.join(storeDir, hit)); else missing.push(wad);
  }
  return { map: s.map, files, missing };
}

// [rc4l] Remove the session files that name port, since a launch there is about to mint a new token and `ready` would wait out its timeout on the old one; returns the files removed.
export function clearPortSessions(sessionFile, port) {
  const gone = [];
  for (const f of [sessionFile, sessionFileFor(sessionFile, port)]) {
    const s = readSession(f);
    if (s && s.port === Number(port)) { fs.rmSync(f, { force: true }); gone.push(f); }
  }
  return gone;
}

// [rc4l] The file a play launch on port holds while it checks its world, naming the fuactl process doing it (gitignored with the session files).
export function launchFileFor(sessionFile, port) { return `${sessionFileFor(sessionFile, port)}.launching`; }

// [rc4l] Runs fn with port's launch file held, so a command started meanwhile waits for it (awaitLaunch) instead of knocking it off the bridge.
export async function holdingLaunchFile(sessionFile, port, fn, pid = process.pid) {
  const file = launchFileFor(sessionFile, port);
  fs.writeFileSync(file, `PORT=${port}\nPID=${pid}\n`);
  try { return await fn(); } finally { fs.rmSync(file, { force: true }); }
}

// [rc4l] Whether a launch file names a launch still running, since one left by a killed launch names a dead process.
export function launchInProgress(file, isAlive = pidAlive) {
  const s = readSession(file);
  return !!(s && s.pid && isAlive(s.pid));
}

// [rc4l] Waits up to timeoutMs while a launch checks its world on port, because the bridge serves only its newest client and connecting then killed the launch with "hello timeout"; true if it waited.
export async function awaitLaunch(sessionFile, port, { timeoutMs = 120000, pollMs = 250, isAlive = pidAlive } = {}) {
  const file = launchFileFor(sessionFile, port);
  let waited = false;
  for (const deadline = Date.now() + timeoutMs; launchInProgress(file, isAlive) && Date.now() < deadline; waited = true) await sleep(pollMs);
  return waited;
}

// [rc4l] The spot name `quit --park` files an instance under, in its own file beside the session file (gitignored with it).
export const PARKED = "parked";
export function parkFileFor(sessionFile, port) { return `${sessionFileFor(sessionFile, port)}.park`; }

// [rc4l] How `play --unpark` relaunches a parked instance: spotLaunch's map and wads, its camera, whether its Vulkan window was un-embedded, and whether it had no GL.
export function parkedLaunch(parkPath, storeDir, listing) {
  const s = JSON.parse(fs.readFileSync(parkPath, "utf8")).spots[PARKED];
  const embed = String((s && s.renderer && s.renderer.fua_dg_embed) ?? "");
  return { ...spotLaunch(parkPath, PARKED, storeDir, listing), at: readSpot(parkPath, PARKED),
    sideBySide: embed === "false" || embed === "0", noGl: !!(s && s.renderer && s.renderer.backend === "vulkan") };
}

// [rc4l] Which instance a command means: --port and --token, else the last play session's.
export function resolveTarget(flags, session) {
  // [rc4l] A session for the same port lends its token and the binary it launched, so `--port 7801` alone finds the user's instance and where it writes its captures.
  if (flags.port) {
    const same = session && Number(session.port) === Number(flags.port);
    return { port: Number(flags.port), token: flags.token || (same ? session.token : null), engine: (same && session.engine) || null,
      pid: (same && !flags.token && session.pid) || null };
  }
  if (session) return { port: Number(session.port), token: flags.token || session.token || null, engine: session.engine || null,
    pid: (!flags.token && session.pid) || null };
  return null;
}

// [rc4l] Whether a process is running: signal 0 only asks, and EPERM means it runs as someone else.
export function pidAlive(pid) {
  try { process.kill(pid, 0); return true; } catch (e) { return e.code === "EPERM"; }
}

// [rc4l] Whether `ready` may connect to a target: it has a token, and the engine its session names is alive, since the bridge serves only its newest client and a stale session's token would knock a launch still checking its world off it.
export function liveTarget(t, isAlive = pidAlive) {
  return !!(t && t.token && (!t.pid || isAlive(t.pid)));
}

// [rc4l] A cvar's value from the console's `"name" is "value"` echo, or null when the echo is not one.
export function cvarValueFrom(echo) {
  const m = String(echo).match(/is\s+"([^"]*)"/);
  return m ? m[1] : null;
}

// [rc4l] The camera a command was given: --spot NAME, else --at x,y,z with --face yaw,pitch, else none.
export function cameraFromFlags(flags, spotsPath, defaultFace = "0,0") {
  let at = null;
  if (flags.spot) at = readSpot(spotsPath, String(flags.spot));
  else if (flags.at) {
    const [x, y, z] = String(flags.at).split(",").map(Number);
    const [angle, pitch] = String(flags.face || defaultFace).split(",").map(Number);
    at = { x, y, z, angle, pitch: pitch || 0 };
  }
  // [rc4l] --force stands the camera there even when something occupies the spot, which a spot saved beside a monster or a door needs.
  if (at && flags.force) at.force = 1;
  return at;
}

// [rc4l] Wait for the FILE, not for a guess about how long the engine takes to write it.
//
// These were fixed sleeps totalling seven seconds a capture, which was most of a capture -- and
// still occasionally short, producing a zero-byte png that read as a renderer that drew nothing.
async function waitForFile(p, { timeoutMs = 10000 } = {}) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try { if (fs.statSync(p).size > 0) return true; } catch { /* not yet */ }
    await sleep(250);
  }
  return false;
}

// A PNG whose IHDR says 1x1 is a swapchain that had no window yet, not a picture. The header is at a
// fixed offset, so this needs no decoder.
function pngIsUsable(file) {
  try {
    const fd = fs.openSync(file, "r");
    const head = Buffer.alloc(24);
    fs.readSync(fd, head, 0, 24, 0);
    fs.closeSync(fd);
    if (head.toString("latin1", 1, 4) !== "PNG") return false;
    return !(head.readUInt32BE(16) <= 1 && head.readUInt32BE(20) <= 1);
  } catch { return false; }
}

// [rc4l] The engine writes its GL screenshot into sweep/ beside its own binary, so a pair lands there whichever build is running.
export function sweepDirFor(engineBin) {
  return path.posix.join(path.dirname(String(engineBin)).split(path.sep).join("/"), "sweep");
}

// [rc4l] Stand the player at a camera with the sim running, so the view and everything it sees settle there.
// [rc4l] Stand the player at at, and when the engine refuses the spot as solid or occupied stand there anyway, saying so: a spot saved beside a monster or a door failed whole launches and every shot without --force.
export async function standAt(c, at, log = console.error) {
  const pos = { x: at.x, y: at.y, z: at.z, angle: at.angle, pitch: at.pitch || 0, ...(at.force ? { force: 1 } : {}) };
  try {
    await c.rpc("player.setpos", pos);
  } catch (e) {
    if (at.force || !/solid or occupied/.test(String(e && e.message))) throw e;
    log("the spot is solid or occupied -- standing there anyway");
    await c.rpc("player.setpos", { ...pos, force: 1 });
  }
}

export async function moveTo(c, at) {
  await c.rpc("sim.resume", {}).catch(() => {});
  await waitTics(c, 10);
  await standAt(c, at);
  await waitTics(c, 10);
}

// [rc4l] A Vulkan swapchain capture, retried while it comes back 1x1 from a window not yet sized; null if it never arrives.
export async function captureVulkan(c, file) {
  for (let attempt = 0; attempt < 4; attempt++) {
    try { fs.rmSync(file, { force: true }); } catch { /* fine */ }
    await exec(c, `fua_diligent_shot ${file}`);
    if (await waitForFile(file) && pngIsUsable(file)) return file;
    await sleep(250);
  }
  return null;
}

// [rc4l] The camera player.camera reports, as the position standAt and a spot take.
export function atFromCamera(cam) { return { x: cam.x, y: cam.y, z: cam.z, angle: cam.yaw, pitch: cam.pitch }; }

// [rc4l] The console line that captures both renderers in one engine frame, for a picture that changes between two (an ANIMDEFS texture runs on the clock, paused or not).
export function sameFrameCommand(tag, vkFile) { return `screenshot sweep/${tag}_gl; fua_diligent_shot ${vkFile}`; }

// [rc4l] Both renderers' frames into outDir as <tag>_gl.png and <tag>_vk.png, the sim already paused; sameFrame takes them from one engine frame.
async function capturePausedPair(c, tag, { engineBin, outDir, sameFrame = false }) {
  const gl = path.posix.join(outDir, `${tag}_gl.png`);
  const vk = path.posix.join(outDir, `${tag}_vk.png`);
  for (const f of [gl, vk]) { try { fs.rmSync(f, { force: true }); } catch { /* fine */ } }
  if (sameFrame) {
    await exec(c, sameFrameCommand(tag, vk));
    const gotGl = await waitForFile(gl);
    return { gl: gotGl ? gl : null, vk: (await waitForFile(vk)) && pngIsUsable(vk) ? vk : null };
  }

  // [rc4l] A blank GL frame is the readback's fault, not the renderer's, and a second ask inside the same pause comes back whole.
  let gotGl = false;
  for (let attempt = 0; attempt < 3; attempt++) {
    try { fs.rmSync(gl, { force: true }); } catch { /* fine */ }
    await ui.screenshot(c, engineBin, `sweep/${tag}_gl`).catch(() => {});
    gotGl = await waitForFile(gl);
    if (!gotGl || !isBlankImage(decodePNG(gl))) break;
    // [rc4l] A minimized window reads back blank every time, so un-minimize it and let a frame draw before asking again.
    if ((await exec(c, "fua_window_restore")).includes("restored")) await sleep(300);
  }
  const gotVk = await captureVulkan(c, vk);
  return { gl: gotGl ? gl : null, vk: gotVk };
}

// [rc4l] One camera, both renderers, one paused frame; `frozen` commands run inside the pause, since anything asked after it describes a later world.
export async function shotPair(c, tag, {
  at = null, engineBin, outDir = null, frozen = [], sameFrame = false,
} = {}) {
  outDir = outDir || sweepDirFor(engineBin);
  fs.mkdirSync(outDir, { recursive: true });
  if (at) await moveTo(c, at);
  await c.rpc("sim.pause", {}).catch(() => {});
  const pair = await capturePausedPair(c, tag, { engineBin, outDir, sameFrame });

  const said = [];
  for (const cmd of frozen) said.push(await exec(c, cmd));

  await c.rpc("sim.resume", {}).catch(() => {});
  return { ...pair, frozen: said };
}

// [rc4l] The command that lets go of a held action, "-crouch" for "+crouch".
export function releaseOf(hold) { return String(hold).replace(/^\+/, "-"); }

// [rc4l] One tic's name in a stepped series, zero-padded so the files sort in order.
export function stepTag(tag, i) { return `${tag}_${String(i).padStart(2, "0")}`; }

// [rc4l] An action held from a settled start and stepped one tic at a time, a pair and the eye height each tic, for a fault that shows for a tic or two of a transition.
export async function captureSteps(c, { tag, hold, count = 8, engineBin, outDir = null, settleTics = 35 }) {
  outDir = outDir || sweepDirFor(engineBin);
  fs.mkdirSync(outDir, { recursive: true });
  await exec(c, releaseOf(hold));
  await c.rpc("sim.resume", {}).catch(() => {});
  await waitTics(c, settleTics);
  await c.rpc("sim.pause", {}).catch(() => {});
  await exec(c, hold);
  const steps = [];
  try {
    for (let i = 0; i < count; i++) {
      await c.rpc("sim.step", { tics: 1 });
      const cam = await c.rpc("player.camera", {});
      const name = stepTag(tag, i);
      steps.push({ name, viewz: cam.viewz, ...(await capturePausedPair(c, name, { engineBin, outDir })) });
    }
  } finally {
    await exec(c, releaseOf(hold));
    await c.rpc("sim.resume", {}).catch(() => {});
  }
  return steps;
}

// [rc4l] count captures from one renderer as fast as it gives them, sim RUNNING, each with its time and box mean: an animation's rate, or a light a pause would hide.
export async function captureBurst(c, { side = "vk", count = 8, box = [0, 0, 1, 1], engineBin, outDir = null, tag = "burst" }) {
  outDir = outDir || sweepDirFor(engineBin);
  fs.mkdirSync(outDir, { recursive: true });
  await c.rpc("sim.resume", {}).catch(() => {});
  const out = [];
  const t0 = Date.now();
  for (let i = 0; i < count; i++) {
    const t = Date.now() - t0;
    const file = side === "gl"
      ? (await ui.screenshot(c, engineBin, `sweep/${tag}_gl${i}`)).path
      : await captureVulkan(c, path.posix.join(outDir, `${tag}_vk${i}.png`));
    out.push({ i, t, file, mean: file ? boxMean(decodePNG(file), box) : NaN });
  }
  return out;
}

// [rc4l] Two captures intervalMs apart with the sim RUNNING, Vulkan and optionally GL, for what must move: a warp, a scroller, an animation.
export async function captureOverTime(c, { at = null, engineBin, outDir, tag = "motion", intervalMs = 600, gl = false }) {
  outDir = outDir || sweepDirFor(engineBin);
  fs.mkdirSync(outDir, { recursive: true });
  if (at) await moveTo(c, at);
  await c.rpc("sim.resume", {}).catch(() => {});
  const vk = [], glFiles = [];
  for (let i = 0; i < 2; i++) {
    if (i > 0) await sleep(intervalMs);
    vk.push(await captureVulkan(c, path.posix.join(outDir, `${tag}_vk${i}.png`)));
    if (gl) glFiles.push((await ui.screenshot(c, engineBin, `sweep/${tag}_gl${i}`).catch(() => ({ path: null }))).path);
  }
  return { vk, gl: gl ? glFiles : null };
}

// [rc4l] A cvar's two values in ONE paused frame, then the first again, so animated lights cannot pass for a difference; the cvar is left as found.
export async function frozenAB(c, { cvar, values: [a, b], at = null, outDir, tag = "ab", settleMs = 400 }) {
  fs.mkdirSync(outDir, { recursive: true });
  const was = cvarValueFrom(await exec(c, cvar));
  if (at) await moveTo(c, at);
  await c.rpc("sim.pause", {}).catch(() => {});
  const files = [];
  try {
    for (const [i, v] of [a, b, a].entries()) {
      await exec(c, `${cvar} ${v}`);
      await sleep(settleMs);
      files.push(await captureVulkan(c, path.posix.join(outDir, `${tag}_${i}_${v}.png`)));
    }
  } finally {
    await exec(c, `${cvar} ${was === null ? a : was}`).catch(() => {});
    await c.rpc("sim.resume", {}).catch(() => {});
  }
  return files;
}

// [rc4l] Build the engine and stage it, failing LOUDLY.
//
// Written after two measurements were taken against a stale binary: the build ran inside a shell
// one-liner that ended with a copy, so the exit status came from the copy and a compile error
// scrolled past as ordinary output while the run happily continued. A silent stale build is worse
// than a broken one -- it produces numbers and screenshots that look real.
// [rc4l] FUACTL_CMAKE, else the CMake a Visual Studio 2022 install ships, else whatever `cmake` is on PATH.
export function pickCmake(env = process.env, exists = fs.existsSync) {
  if (env.FUACTL_CMAKE) return env.FUACTL_CMAKE;
  const vs = path.join(env.ProgramFiles || "C:/Program Files", "Microsoft Visual Studio/2022");
  for (const edition of ["Community", "Professional", "Enterprise", "BuildTools"]) {
    const cm = path.join(vs, edition, "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
    if (exists(cm)) return cm;
  }
  return "cmake";
}

// [rc4l] The checkout this copy of fuactl lives in, so `build` works from any clone.
const REPO_ROOT = fileURLToPath(new URL("../../../", import.meta.url));

// [rc4l] A build log's first errors, and whether the linker could not write the binary because an engine still holds it open (LNK1104).
export function buildErrors(out) {
  return { errors: out.split(/\r?\n/).filter((l) => /error (C|LNK|MSB)\d*/.test(l)).slice(0, 20), locked: /LNK1104/.test(out) };
}

// [rc4l] A unit test run's count of passing tests and the names of the failing ones, from gtest's own summary lines.
export function summarizeGtest(out) {
  const passed = /\[\s+PASSED\s+\]\s+(\d+)\s+test/.exec(out);
  const failed = [...out.matchAll(/^\[\s+FAILED\s+\]\s+(\S+\.\S+)/gm)].map((m) => m[1]);
  return { passed: passed ? Number(passed[1]) : 0, failed: [...new Set(failed)] };
}

// [rc4l] Sources ending in one of suffixes as one sorted listing, so a build can tell when a new one appeared; the trees glob them at configure time only.
export function sourcesStamp(files) {
  return [...files].map((f) => f.split(String.fromCharCode(92)).join("/")).sort().join("\n");
}

function listSources(dir, suffixes) {
  return fs.readdirSync(dir, { recursive: true }).map(String).filter((f) => suffixes.some((s) => f.endsWith(s)));
}

// [rc4l] Configure dir again when the globbed sources changed since it last was, so a new *_compute.cpp or *_test.cpp builds without a CMake edit.
function refreshGlobs(cm, root, dir, suffixes, log) {
  const stamp = sourcesStamp(listSources(path.join(root, "src/zandronum/src"), suffixes));
  const file = path.join(root, dir, ".globbed-sources");
  if (fs.existsSync(file) && fs.readFileSync(file, "utf8") === stamp) return;
  const r = spawnSync(cm, [path.join(root, dir)], { encoding: "utf8", env: { ...process.env, DXSDK_DIR: path.join(root, "dxsdk") } });
  if (r.status !== 0) {
    log(((r.stdout || "") + (r.stderr || "")).split(/\r?\n/).filter((l) => /error/i.test(l)).slice(0, 10).join("\n"));
    throw new Error(`configuring ${dir} again failed`);
  }
  fs.writeFileSync(file, stamp);
}

// [rc4l] One cmake build of the tree in dir (one target, or all of it), its log beside it, throwing with the first errors when it fails.
function compile(cm, root, dir, target, log) {
  const r = spawnSync(cm, ["--build", path.join(root, dir), "--config", "Release", ...(target ? ["--target", target] : []), "--", "-m"], {
    encoding: "utf8", env: { ...process.env, DXSDK_DIR: path.join(root, "dxsdk") },
  });
  const out = (r.stdout || "") + (r.stderr || "");
  fs.mkdirSync(path.join(root, dir), { recursive: true });
  fs.writeFileSync(path.join(root, dir, "build.log"), out);
  const { errors, locked } = buildErrors(out);
  // A zero exit with errors in the log has happened too; belt and braces.
  if (r.status !== 0 || errors.length) {
    for (const e of errors) log(e);
    if (locked) throw new Error(`a running engine holds ${dir}'s binary -- quit it by port first`);
    throw new Error(r.status !== 0 ? "build failed" : "build reported success but the log contains errors");
  }
}

// [rc4l] Compile the engine, and with tests the unit tests too and run them; then stage it unless stage is false, which leaves the user's binary alone mid-investigation.
export function build({ root = REPO_ROOT, cmake = null, log = console.error, stage = true, tests = false } = {}) {
  const cm = cmake || pickCmake();
  refreshGlobs(cm, root, "build-win", ["_compute.cpp"], log);
  compile(cm, root, "build-win", "zdoom", log);
  let testRun = null;
  if (tests) {
    refreshGlobs(cm, root, "build-tests", ["_compute.cpp", "_test.cpp"], log);
    compile(cm, root, "build-tests", null, log);
    const r = spawnSync(path.join(root, "build-tests/Release/zandrox_tests.exe"), ["--gtest_brief=1"], { encoding: "utf8" });
    testRun = summarizeGtest((r.stdout || "") + (r.stderr || ""));
    if (r.status !== 0) {
      for (const f of testRun.failed) log(`FAILED ${f}`);
      throw new Error(`unit tests failed (${testRun.failed.length})`);
    }
  }
  if (!stage) return { staged: null, entries: 0, tests: testRun };

  // [rc4l] A running engine holds the staged binary, and killing it by name would take the user's own instance with it.
  const running = engineProcessCount();
  if (running > 0) {
    throw new Error(`${running} engine(s) hold the staged binary -- quit each by port (ui exec quit), or run the build with FUACTL_ENGINE`);
  }

  // [rc4l] The lock can outlive a quit by a beat, so the copy retries instead of reading EBUSY as a failed build.
  const from = path.join(root, "build-win/Release/forkundera.exe");
  const to = path.join(root, "dist-windows/forkundera.exe");
  let lastErr = null;
  for (let i = 0; i < 40; i++) {
    try { fs.copyFileSync(from, to); lastErr = null; break; }
    catch (e) { lastErr = e; Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 250); }
  }
  if (lastErr) throw new Error(`could not stage over the running binary after 10s: ${lastErr.message}`);

  // [rc4l] The addon catalogue, alongside the binary, because the HOST tab reads it from progdir.
  // Staging only the exe meant every build had no catalogue at all -- the presets list drew empty
  // and hosting read as unimplemented rather than unstaged.
  const cat = path.join(root, "catalogue");
  if (!fs.existsSync(cat)) throw new Error("catalogue/ missing -- the HOST tab would have nothing to offer");
  fs.rmSync(path.join(root, "dist-windows/catalogue"), { recursive: true, force: true });
  fs.cpSync(cat, path.join(root, "dist-windows/catalogue"), { recursive: true });

  return { staged: path.join(root, "dist-windows/forkundera.exe"), entries: fs.readdirSync(cat).length, tests: testRun };
}
