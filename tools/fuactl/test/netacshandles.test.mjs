import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeClients, judgeCopies, judgeEither, COPY_STEPS, CONFIGS, buildCopiesPk3, buildNetHandlesPk3 } from "../scenarios/netacshandles.mjs";

// [Doruk-Aksoy] Script 1's log for handle h: the player's own handle 9, the activator taken and kept.
const h1 = (h) => `h1 ${h} ${h} 1 0 0 0 1 ${h} ${h} 9 1 20 1 0 1`;

test("the net id judge wants a server handle and a valid client handle to the barrel", () => {
  assert.deepEqual(judgeClients(3, { a: { h: 2, ok: 1, barrel: 1 } }), []);
  assert.equal(judgeClients(0, { a: { h: 0, ok: 0, barrel: 0 } }).length, 2);
});

test("each machine's copies run: its handles, and on a client that saw the removal a stale handle and no actor for the tid", () => {
  assert.deepEqual(judgeCopies({ h1: h1(4), h7: "h7 1" }, { role: "server" }), []);
  assert.match(judgeCopies({ h1: h1(4), h7: "" }, { role: "server" }).join(), /never removed/);
  const client = { h1: h1(4), h6: "h6 7 1", h8: "h8 0 0" };
  assert.deepEqual(judgeCopies(client, { role: "observer" }), []);
  assert.match(judgeCopies({ ...client, h8: "h8 1 3" }, { role: "player" }).join(), /read valid 1 and the tid gave handle 3/);
  assert.match(judgeCopies({ ...client, h6: "h6 0 0" }, { role: "player" }).join(), /second barrel was 0/);
  assert.deepEqual(judgeCopies({ h1: h1(4) }, { role: "latejoin" }), []);
});

test("the removal runs on the server alone and the reads on the clients alone, and copies configs bring their own pk3", () => {
  assert.deepEqual(COPY_STEPS.map((s) => [s.script, s.clients !== false, s.server !== false]), [[1, true, true], [6, true, false], [7, false, true], [8, true, false]]);
  const copies = CONFIGS.filter((c) => c.copies);
  assert.equal(copies.length, 2);
  for (const c of copies) assert.ok(c.wad.toString("latin1").includes("maps/MAP01.wad") && c.observers === 1);
  assert.ok(buildCopiesPk3().length > buildNetHandlesPk3().length);
  assert.equal(typeof judgeEither, "function");
});
