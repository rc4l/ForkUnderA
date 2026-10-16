import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { nativePaths, needsBuild, ensureNative } from "../src/native.mjs";

test("a helper lives in its own folder, its executable under the build tree's Release", () => {
  const p = nativePaths("stackdump", "/n");
  assert.equal(p.source, path.join("/n", "stackdump", "stackdump.cpp"));
  assert.equal(p.exe, path.join("/n", "stackdump", "build", "Release", "stackdump.exe"));
});

test("a helper is built when missing or older than its source, and not otherwise", () => {
  const p = nativePaths("screencap", "/n");
  assert.equal(needsBuild(p, (f) => (f === p.exe ? null : 5)), true);
  assert.equal(needsBuild(p, (f) => (f === p.exe ? 4 : 5)), true);
  assert.equal(needsBuild(p, (f) => (f === p.exe ? 6 : 5)), false);
});

test("building runs cmake configure then build, and a failure names the helper and its errors", () => {
  const p = nativePaths("screencap", "/n");
  const calls = [];
  const exe = ensureNative("screencap", "cmake", { paths: { ...p, exe: "/nowhere/screencap.exe" }, run: (cm, args) => { calls.push(args[0]); return { status: 0 }; } });
  assert.equal(exe, "/nowhere/screencap.exe");
  assert.deepEqual(calls, ["-S", "--build"]);
  assert.throws(() => ensureNative("screencap", "cmake", { paths: { ...p, exe: "/nowhere/screencap.exe" }, run: () => ({ status: 1, stdout: "x\nerror C2065: nope\n" }) }), /screencap build failed: error C2065/);
  const fresh = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-native-")), "screencap.exe");
  fs.writeFileSync(fresh, "");
  assert.equal(ensureNative("screencap", "cmake", { paths: { source: fresh, build: "/b", exe: fresh }, run: () => { throw new Error("must not build"); } }), fresh);
});
