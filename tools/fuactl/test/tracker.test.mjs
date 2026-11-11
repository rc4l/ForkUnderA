import { test } from "node:test";
import assert from "node:assert/strict";
import path from "node:path";
import { findHg, findPython, pythonCandidates, runTracker, trackerArgs, TRACKER_USAGE } from "../src/tracker.mjs";

// [rc4l] A fake spawnSync: each command name maps to the exit status it returns, and every call is recorded.
function fakeRun(status) {
  const calls = [];
  const run = (cmd, args, opts) => { calls.push({ cmd, args, opts }); return { status: cmd in status ? status[cmd] : null }; };
  return { run, calls };
}

test("python candidates put $PYTHON first and the launcher last on Windows only", () => {
  assert.deepEqual(pythonCandidates({ PYTHON: "C:/Py/python.exe" }, "win32"), [["C:/Py/python.exe"], ["python3"], ["python"], ["py", "-3"]]);
  assert.deepEqual(pythonCandidates({}, "linux"), [["python3"], ["python"]]);
});

test("the Store stub's python3 is skipped for the first interpreter that runs Python 3", () => {
  const { run, calls } = fakeRun({ python3: 9009, python: 1, py: 0 });
  assert.deepEqual(findPython({ env: {}, platform: "win32", run }), ["py", "-3"]);
  assert.equal(calls.length, 3);
  assert.deepEqual(calls[2].args.slice(0, 2), ["-3", "-c"]);
});

test("no Python at all gives null", () => {
  const { run } = fakeRun({});
  assert.equal(findPython({ env: {}, platform: "linux", run }), null);
});

test("Mercurial comes from $HG, else PATH, else nothing", () => {
  assert.equal(findHg({ env: { HG: "D:/hg/hg.exe" }, run: fakeRun({ "D:/hg/hg.exe": 0 }).run }), "D:/hg/hg.exe");
  assert.equal(findHg({ env: { HG: "missing" }, run: fakeRun({ hg: 0 }).run }), "hg");
  assert.equal(findHg({ env: {}, run: fakeRun({}).run }), null);
});

test("tracker.py gets the launcher's arguments, the script, then the command as given", () => {
  const root = path.join("r", "fork");
  assert.deepEqual(trackerArgs(root, ["py", "-3"], ["mark", "abc", "skip", "--note", "a b"]),
    ["-3", path.join(root, "commit-tracker", "tracker.py"), "mark", "abc", "skip", "--note", "a b"]);
});

test("runTracker passes the found Mercurial on and returns tracker.py's status", () => {
  const { run, calls } = fakeRun({ python3: 0, hg: 0 });
  const status = runTracker("/r", ["next"], { env: {}, run, log: () => {} });
  const last = calls[calls.length - 1];
  assert.equal(status, 0);
  assert.equal(last.cmd, "python3");
  assert.equal(last.opts.env.HG, "hg");
  assert.equal(last.opts.cwd, "/r");
});

test("no command prints the usage and fails, help prints it and succeeds, no Python fails with 2", () => {
  const said = [];
  assert.equal(runTracker("/r", [], { log: (m) => said.push(m) }), 2);
  assert.equal(said[0], TRACKER_USAGE);
  assert.equal(runTracker("/r", ["help"], { log: () => {} }), 0);
  assert.equal(runTracker("/r", ["next"], { env: {}, run: fakeRun({}).run, log: () => {} }), 2);
});
