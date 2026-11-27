import { test } from "node:test";
import assert from "node:assert/strict";
import { buildContinueDisconnectWad, judgeReturn, STOOD_WITHIN, CONFIGS } from "../scenarios/continuedisconnect.mjs";

test("the netgame's map is a room of its own with player starts", () => {
  const bytes = buildContinueDisconnectWad().toString("latin1");
  assert.ok(bytes.includes("MAP01") && bytes.includes("TEXTMAP"));
  assert.deepEqual(CONFIGS, [{ name: "lan", players: 1, serverCvars: { fua_snap: 1, fua_snap_send: 1, fua_snap_nolegacy: 2 } }]);
});

test("a client back where the saved game was left passes", () => {
  assert.deepEqual(judgeReturn({ role: "single", x: 10, y: 20 }, { x: 10 + STOOD_WITHIN, y: 20 }), []);
});

// [Doruk-Aksoy] The old build loaded the save before its restart and crashed, so nothing came back.
test("a client that never came back, stayed online or stands elsewhere fails, saying which", () => {
  assert.deepEqual(judgeReturn(null, { x: 0, y: 0 }), ["the client never came back up in a level after its disconnect"]);
  assert.deepEqual(judgeReturn({ role: "client", x: 0, y: 0 }, { x: 0, y: 0 }), ["the client is client after its disconnect, not single"]);
  assert.deepEqual(judgeReturn({ role: "single", x: 30, y: 40 }, { x: 0, y: 0 }), ["the client stands 50.0 units from where the saved game was left"]);
});
