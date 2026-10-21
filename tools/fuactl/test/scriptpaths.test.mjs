import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { FUACTL_DIR, CLI, PLAY_SESSION, STAGED_ENGINE, STAGED_SWEEP, scratchDir } from "../src/scriptpaths.mjs";

// [rc4l] The scripts' paths are this checkout's, written with forward slashes so they survive an engine console command.
test("script paths sit in this checkout and use forward slashes", () => {
  assert.ok(fs.existsSync(CLI));
  assert.equal(PLAY_SESSION, `${FUACTL_DIR}/.play-session`);
  assert.equal(STAGED_ENGINE, `${path.resolve(FUACTL_DIR, "../..").split(path.sep).join("/")}/dist-windows/forkundera.exe`);
  assert.equal(STAGED_SWEEP, STAGED_ENGINE.replace(/forkundera\.exe$/, "sweep"));
  for (const p of [FUACTL_DIR, CLI, PLAY_SESSION, STAGED_ENGINE, STAGED_SWEEP]) assert.ok(!p.includes("\\"), p);
});

test("scratch is FUACTL_SCRATCH when set, else the system temp folder", () => {
  assert.equal(scratchDir({ FUACTL_SCRATCH: "/somewhere" }), "/somewhere");
  assert.equal(scratchDir({}), os.tmpdir().split(path.sep).join("/"));
});
