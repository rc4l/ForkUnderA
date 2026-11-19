import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { uzdoomExe, uzdoomCommandLine, logFileIn, runUzdoom, newestPng, uzdoomScreenshot, LOG_NAME } from "../src/uzdoom.mjs";

test("the UZDoom to run is FUACTL_UZDOOM, else the Release build beside the repository", () => {
  assert.equal(uzdoomExe({ FUACTL_UZDOOM: "X:/u/uzdoom.exe" }), "X:/u/uzdoom.exe");
  assert.match(uzdoomExe({}), /UZDoom-build[\\/]Release[\\/]uzdoom\.exe$/);
});

test("the log is a bare name, since UZDoom writes log-<name>.txt in its working directory and fails on a path", () => {
  const line = uzdoomCommandLine({ exe: "u.exe", iwad: "f.wad", pk3: "p.pk3", map: "MAP02", chain: "wait 35;puke 1;quit", dir: "D", extra: ["+crosshair", 0] });
  assert.match(line, /\+logfile uzdoom \+crosshair 0 \+map MAP02 /);
  assert.equal(LOG_NAME, "uzdoom");
  assert.equal(logFileIn("D"), path.join("D", "log-uzdoom.txt"));
  assert.match(line, /^"u\.exe" -iwad "f\.wad" -file "p\.pk3" -config "D[\\/]uzdoom\.ini" -nosound -nomusic/);
  assert.match(line, /\+map MAP02 \+wait 35;puke 1;quit$/);
  assert.match(line, / \+i_pauseinbackground 0 /);
  assert.doesNotMatch(uzdoomCommandLine({ exe: "u.exe", map: "MAP01", chain: "quit", dir: "D" }), /-file/);
});

// [Doruk-Aksoy] A fake UZDoom: a folder with an exe and a launcher that logs, and screenshots, only on the run `on`.
function fake(on, png = true) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "uzdoom-test-"));
  const exe = path.join(dir, "uzdoom.exe");
  fs.writeFileSync(exe, "");
  const calls = [];
  const run = (cmd, args, opts) => {
    calls.push(opts.env.FUACTL_LAUNCH_CMDLINE);
    const at = args[args.indexOf("-Dir") + 1];
    if (calls.length === on) {
      fs.writeFileSync(logFileIn(at), "parity 1\n");
      if (png) fs.writeFileSync(path.join(at, "shot.png"), "");
    }
  };
  return { dir, exe, calls, run };
}

test("a run reads the log UZDoom wrote, tries again when it wrote none, and needs a UZDoom", () => {
  const f = fake(2);
  assert.equal(runUzdoom({ exe: f.exe, map: "MAP01", chain: "quit", dir: f.dir, run: f.run }), "parity 1\n");
  assert.equal(f.calls.length, 2);
  assert.match(f.calls[1], /-config "[^"]*try2[\\/]uzdoom\.ini"/);
  assert.equal(runUzdoom({ exe: f.exe, map: "MAP01", chain: "quit", dir: path.join(f.dir, "none"), run: () => {}, tries: 1 }), "");
  assert.throws(() => runUzdoom({ exe: path.join(f.dir, "missing.exe"), map: "MAP01", chain: "quit", dir: f.dir }), /no UZDoom at .*FUACTL_UZDOOM/);
});

test("a screenshot is the newest picture the run left, without the crosshair or weapon, or an error with the log's end", () => {
  const f = fake(1);
  const png = uzdoomScreenshot({ exe: f.exe, pk3: "p.pk3", map: "MAP03", dir: f.dir, run: f.run });
  assert.equal(path.basename(png), "shot.png");
  assert.match(f.calls[0], /\+crosshair 0 \+r_drawplayersprites 0 \+map MAP03 \+wait 70;screenshot;wait 35;quit$/);
  const none = fake(1, false);
  assert.throws(() => uzdoomScreenshot({ exe: none.exe, map: "MAP03", dir: none.dir, run: none.run }), /no screenshot of MAP03[\s\S]*parity 1/);
  assert.equal(newestPng(fs.mkdtempSync(path.join(os.tmpdir(), "uzdoom-empty-"))), null);
});
