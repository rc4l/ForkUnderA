import { test } from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import { BridgeClient, helloTimeoutMessage } from "../src/client.mjs";

// [rc4l] Two fuactl commands at once ended one with a bare "hello timeout"; a connection the engine takes but never greets now says what that can mean.
test("a connection that is never greeted says why it waited", async () => {
  const held = [];
  const server = net.createServer((s) => held.push(s));
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const c = new BridgeClient();
  try {
    await c.connect(server.address().port);
    await assert.rejects(c.waitHello(50), (e) => e.message === helloTimeoutMessage(50));
  } finally {
    c.close();
    for (const s of held) s.destroy();
    server.close();
  }
  assert.match(helloTimeoutMessage(1500), /^hello timeout: .* within 1\.5 s; it is still starting, hung \(`fuactl hang --port P` says where\), or an older build whose bridge serves one client at a time/);
});

// [rc4l] nuts.wad with Complex Doom ran 6-second frames, and every rpc timed out with no hint of the stretch that fixed it.
test("an rpc timeout names the command, the wait, the stretch and the hang check", async () => {
  const { rpcTimeoutMessage } = await import("../src/client.mjs");
  const m = rpcTimeoutMessage("console.exec", 8000);
  assert.match(m, /^rpc timeout: console\.exec got no answer within 8 s/);
  assert.match(m, /FUACTL_TIMEOUT_SCALE=N/);
  assert.match(m, /fuactl hang --port P/);
});
