// [rc4l] Matched GL/Vulkan pairs, and the build that produces them.
//
// The comparison that matters is the SAME CAMERA in both renderers. Doing it by hand meant a dozen
// ordered calls every time, which is how a stale binary and a wrong-camera comparison both slipped
// through -- and each of those cost a diagnosis, because a pair taken at two cameras disagrees
// everywhere and looks like a renderer that has gone completely wrong.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import * as ui from "./ui.mjs";
import { exec, waitTics } from "./capture.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] Find the running hands-on instance without depending on a pipe still existing.
//
// stdout is whatever the caller redirected it into, and a play session that has to be found again
// -- to capture the thing just walked to -- must not depend on that.
export function readSession(file = ".play-session") {
  if (!fs.existsSync(file)) return null;
  const text = fs.readFileSync(file, "utf8");
  const port = /PORT=(\d+)/.exec(text), token = /TOKEN=(\w+)/.exec(text);
  return port ? { port: Number(port[1]), token: token ? token[1] : null } : null;
}

// A named camera from spots.json. A fault found by looking at it is worth a name, not four numbers
// pasted out of a screenshot.
export function readSpot(spotsPath, name) {
  const db = JSON.parse(fs.readFileSync(spotsPath, "utf8"));
  const s = db.spots[name];
  if (!s) throw new Error(`no such spot: ${name}`);
  return { x: s.x, y: s.y, z: s.z, angle: s.angle, pitch: s.pitch || 0, shows: s.shows };
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

// One camera, both renderers, sim frozen in between so nothing can move.
export async function shotPair(c, tag, {
  at = null, engineBin, outDir = "F:/ForkUnderA/dist-windows/sweep",
} = {}) {
  if (at) {
    await c.rpc("sim.resume", {}).catch(() => {});
    await waitTics(c, 10);
    await c.rpc("player.setpos", { x: at.x, y: at.y, z: at.z, angle: at.angle, pitch: at.pitch || 0 });
    await waitTics(c, 10);
  }
  await c.rpc("sim.pause", {}).catch(() => {});

  const gl = path.posix.join(outDir, `${tag}_gl.png`);
  const vk = path.posix.join(outDir, `${tag}_vk.png`);
  for (const f of [gl, vk]) { try { fs.rmSync(f, { force: true }); } catch { /* fine */ } }

  await ui.screenshot(c, engineBin, `sweep/${tag}_gl`).catch(() => {});
  const gotGl = await waitForFile(gl);
  await exec(c, `fua_diligent_shot ${vk}`);
  const gotVk = await waitForFile(vk);

  await c.rpc("sim.resume", {}).catch(() => {});
  return { gl: gotGl ? gl : null, vk: gotVk ? vk : null };
}

// [rc4l] Build the engine and stage it, failing LOUDLY.
//
// Written after two measurements were taken against a stale binary: the build ran inside a shell
// one-liner that ended with a copy, so the exit status came from the copy and a compile error
// scrolled past as ordinary output while the run happily continued. A silent stale build is worse
// than a broken one -- it produces numbers and screenshots that look real.
export function build({ root = "F:/ForkUnderA", cmake = null, log = console.error } = {}) {
  const logPath = path.join(root, "build-win/build.log");
  const cm = cmake || "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
  const r = spawnSync(cm, ["--build", path.join(root, "build-win"), "--config", "Release", "--target", "zdoom", "--", "-m"], {
    encoding: "utf8", env: { ...process.env, DXSDK_DIR: path.join(root, "dxsdk") },
  });
  const out = (r.stdout || "") + (r.stderr || "");
  fs.mkdirSync(path.dirname(logPath), { recursive: true });
  fs.writeFileSync(logPath, out);

  const errors = out.split(/\r?\n/).filter((l) => /error (C|LNK|MSB)\d*/.test(l)).slice(0, 20);
  // A zero exit with errors in the log has happened too; belt and braces.
  if (r.status !== 0 || errors.length) {
    for (const e of errors) log(e);
    throw new Error(r.status !== 0 ? "build failed" : "build reported success but the log contains errors");
  }

  spawnSync("powershell.exe", ["-NoProfile", "-Command",
    "Get-Process -Name forkundera -ErrorAction SilentlyContinue | Stop-Process -Force"], { encoding: "utf8" });

  fs.copyFileSync(path.join(root, "build-win/Release/forkundera.exe"), path.join(root, "dist-windows/forkundera.exe"));

  // [rc4l] The addon catalogue, alongside the binary, because the HOST tab reads it from progdir.
  // Staging only the exe meant every build had no catalogue at all -- the presets list drew empty
  // and hosting read as unimplemented rather than unstaged.
  const cat = path.join(root, "catalogue");
  if (!fs.existsSync(cat)) throw new Error("catalogue/ missing -- the HOST tab would have nothing to offer");
  fs.rmSync(path.join(root, "dist-windows/catalogue"), { recursive: true, force: true });
  fs.cpSync(cat, path.join(root, "dist-windows/catalogue"), { recursive: true });

  return { staged: path.join(root, "dist-windows/forkundera.exe"), entries: fs.readdirSync(cat).length };
}
