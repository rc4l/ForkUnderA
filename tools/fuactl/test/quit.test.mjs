import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { quitInstance, withLockedInstance, portStillAnswers, play, waitInLevel } from "../src/play.mjs";

// [rc4l] A second launch on a busy port cleared the first's session file, and that engine could no longer be quit by its token.
test("a port an engine still answers on is in use, and one that closes within the grace is not", async () => {
  const ok = async () => ({ close: () => {} });
  const refused = async () => { throw new Error("connect ECONNREFUSED"); };
  assert.equal(await portStillAnswers(7799, { connect: ok, graceMs: 5, pollMs: 1 }), true);
  assert.equal(await portStillAnswers(7799, { connect: refused, graceMs: 5, pollMs: 1 }), false);
  let n = 0;
  const closing = async () => { if (n++ > 0) throw new Error("gone"); return { close: () => {} }; };
  assert.equal(await portStillAnswers(7799, { connect: closing, graceMs: 1000, pollMs: 1 }), false);
});

test("play refuses a port in use and leaves its session files alone", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-play-"));
  const sessionFile = path.join(dir, ".play-session");
  const record = "PORT=7799\nTOKEN=abc\n";
  fs.writeFileSync(`${sessionFile}-7799`, record);
  await assert.rejects(play({ port: 7799, sessionFile }, { connect: async () => ({ close: () => {} }), graceMs: 1 }), /port 7799 is in use/);
  assert.equal(fs.readFileSync(`${sessionFile}-7799`, "utf8"), record);
  fs.rmSync(dir, { recursive: true, force: true });
});

// [rc4l] A bridge that answers for the first `alive` connections and refuses after, recording what it was sent.
function fakeBridge(alive) {
  const sent = [];
  let connects = 0;
  const connect = async (port, token) => {
    if (connects++ >= alive) throw new Error("connect ECONNREFUSED");
    return { rpc: async (cmd, args) => { sent.push({ port, token, cmd, args }); }, close: () => {} };
  };
  return { connect, sent, count: () => connects };
}

test("an instance that goes is quit by its token and reported stopped once its port refuses", async () => {
  const b = fakeBridge(3);
  assert.equal(await quitInstance({ port: "7801", token: "t1" }, { connect: b.connect, pollMs: 1 }), "stopped");
  assert.deepEqual(b.sent, [{ port: 7801, token: "t1", cmd: "console.exec", args: { text: "quit" } }]);
  assert.equal(b.count(), 4);
});

test("nothing listening is absent, and nothing is sent", async () => {
  const b = fakeBridge(0);
  assert.equal(await quitInstance({ port: 7801 }, { connect: b.connect, pollMs: 1 }), "absent");
  assert.deepEqual(b.sent, []);
});

// [rc4l] A launch whose session file was lost has no token, and its bridge turns the quit away.
test("a quit the bridge refuses is reported refused at once, without waiting for the port", async () => {
  let connects = 0;
  const connect = async () => { connects++; return { rpc: async () => { throw new Error("unauthorized"); }, close: () => {} }; };
  assert.equal(await quitInstance({ port: 7799 }, { connect, timeoutMs: 10000, pollMs: 1 }), "refused");
  assert.equal(connects, 2);   // [rc4l] the quit, then one look that the port still answers
});

test("an instance still answering at the deadline is reported running", async () => {
  const b = fakeBridge(Infinity);
  assert.equal(await quitInstance({ port: 7801, token: "t" }, { connect: b.connect, timeoutMs: 20, pollMs: 1 }), "running");
});

test("a quit whose reply never comes, because the engine closed first, still counts", async () => {
  let connects = 0;
  const connect = async () => {
    if (connects++ > 0) throw new Error("connect ECONNREFUSED");
    return { rpc: async () => { throw new Error("bridge closed"); }, close: () => {} };
  };
  assert.equal(await quitInstance({ port: 7801, token: "t" }, { connect, pollMs: 1 }), "stopped");
});

// [rc4l] The template harness and fps share one launch-measure-teardown path, so it must always tear down.
test("a locked instance is launched locked, handed over connected, and stopped and closed after", async () => {
  const seen = [];
  const inst = { port: 7799, token: "t", log: "nowhere.log" };
  const deps = {
    launch: async (o) => { seen.push(`launch lock=${o.lock} map=${o.map}`); return inst; },
    connect: async (port, token) => { seen.push(`connect ${port} ${token}`); return { close: () => seen.push("close") }; },
    stop: async (i) => { seen.push(`stop ${i.port}`); },
  };
  const out = await withLockedInstance({ map: "MAP10" }, async (c, i) => { seen.push(`body ${i.port}`); return 42; }, deps);
  assert.equal(out, 42);
  assert.deepEqual(seen, ["launch lock=true map=MAP10", "connect 7799 t", "body 7799", "close", "stop 7799"]);
});

test("a locked instance that crashed reports its crash log, and one that failed otherwise its own error", async () => {
  const fs = await import("node:fs");
  const os = await import("node:os");
  const path = await import("node:path");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-wl-"));
  const log = path.join(dir, "engine-7799.log");
  const deps = { launch: async () => ({ port: 7799, token: "t", log }), connect: async () => ({ close: () => {} }), stop: async () => {} };
  try {
    await assert.rejects(() => withLockedInstance({}, async () => { throw new Error("capture timed out"); }, deps), /capture timed out/);
    fs.writeFileSync(`${log}.crash`, "boom");
    await assert.rejects(() => withLockedInstance({}, async () => { throw new Error("capture timed out"); }, deps), /engine crashed -- .*\.crash/);
    await assert.rejects(() => withLockedInstance({}, async () => 1, { ...deps, launch: async () => { throw new Error("no engine"); } }), /no engine/);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

// [rc4l] A probe left open after its question timed out kept every later one waiting on a one-client bridge, and still holds a reader thread on this one.
test("waiting for a level closes every probe, the ones whose question failed too", async () => {
  const open = new Set();
  let n = 0;
  const connect = async () => {
    const id = n++;
    open.add(id);
    return {
      rpc: async () => { if (id === 0) throw new Error("rpc timeout: sim.tic"); return { inlevel: id >= 2 }; },
      close: () => open.delete(id),
    };
  };
  const inst = { port: 7799, token: "t", proc: { exitCode: null } };
  assert.equal(await waitInLevel(inst, { connect, pollMs: 1, timeoutMs: 5000 }), true);
  assert.equal(n, 3);
  assert.equal(open.size, 0);
});

test("waiting for a level gives up at its deadline and says so when the engine exits first", async () => {
  const never = async () => { throw new Error("connect ECONNREFUSED"); };
  await assert.rejects(waitInLevel({ port: 7799, proc: { exitCode: null } }, { connect: never, pollMs: 1, timeoutMs: 20 }), /no playable level after 0s/);
  await assert.rejects(waitInLevel({ port: 7799, proc: { exitCode: 1 } }, { connect: never, pollMs: 1, timeoutMs: 20 }), /engine exited .* before reaching a level/);
});
