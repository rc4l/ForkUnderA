import { test } from "node:test";
import assert from "node:assert/strict";
import { quitInstance } from "../src/play.mjs";

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
