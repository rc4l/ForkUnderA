import { test } from "node:test";
import assert from "node:assert/strict";
import { quitInstance, withLockedInstance } from "../src/play.mjs";

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
  const log = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-wl-")), "engine-7799.log");
  const deps = { launch: async () => ({ port: 7799, token: "t", log }), connect: async () => ({ close: () => {} }), stop: async () => {} };
  await assert.rejects(() => withLockedInstance({}, async () => { throw new Error("capture timed out"); }, deps), /capture timed out/);
  fs.writeFileSync(`${log}.crash`, "boom");
  await assert.rejects(() => withLockedInstance({}, async () => { throw new Error("capture timed out"); }, deps), /engine crashed -- .*\.crash/);
  await assert.rejects(() => withLockedInstance({}, async () => 1, { ...deps, launch: async () => { throw new Error("no engine"); } }), /no engine/);
});
