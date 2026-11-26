import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeHandles, logged, counts, slotOf, generationOf, buildAcsHandlesPk3, FN } from "../scenarios/acshandles.mjs";
import { judgeHub, buildAcsHandlesHubPk3, MAPINFO, ROUNDS } from "../scenarios/acshandleshub.mjs";
import { judgeClients, buildNetHandlesPk3, sendScript, clientScript } from "../scenarios/netacshandles.mjs";
import { PCD } from "../src/acs.mjs";

const H = 1, PLAYER = 2, SPAWNED = (1 << 20) | 3, REMOVED = 3;
const good = {
  h1: `h1 ${H} ${H} 1 0 0 0 1 ${H} ${H} ${PLAYER} 1 20 1 0 1`,
  h2: `h2 ${REMOVED} 0 ${SPAWNED} 1`,
  counts2: "fua_handles: live 3 parked 0 free 0 retired 0",
  h3: `h3 1 ${H} 0 1 1 1`,
  h4: `h4 0 0 ${(1 << 20) | 1} ${H}`,
  counts4: "fua_handles: live 2 parked 0 free 1 retired 0",
  h5: "h5 0 1",
};

test("a handle splits into its slot and generation", () => {
  assert.equal(slotOf(SPAWNED), 3);
  assert.equal(generationOf(SPAWNED), 1);
  assert.deepEqual(FN, { GetActorHandle: 24020, HandleValid: 24021, HandlePointer: 24022, SetActivatorHandle: 24023, SetPointer: 38 });
});

test("the single-player run passes with every step's answer and names each wrong one", () => {
  assert.deepEqual(judgeHandles(good), []);
  assert.match(judgeHandles({ ...good, h2: `h2 ${REMOVED} 0 ${(2 << 20) | 3} 1` }).join(), /generation 2, not slot 3 generation 1/);
  assert.match(judgeHandles({ ...good, h5: "h5 0 7" }).join(), /first handle was 7/);
  assert.match(judgeHandles({ ...good, counts4: "fua_handles: live 2 parked 1 free 1 retired 0" }).join(), /slots parked/);
  assert.match(judgeHandles({ ...good, h3: "nothing" }).join(), /script h3 logged nothing/);
});

test("logs and counts parse, and absent ones read null", () => {
  assert.deepEqual(logged("x h2 4 -1 5", "h2"), [4, -1, 5]);
  assert.equal(logged("none", "h2"), null);
  assert.deepEqual(counts("fua_handles: live 1 parked 2 free 3 retired 4"), { live: 1, parked: 2, free: 3, retired: 4 });
  assert.equal(counts(""), null);
});

test("the hub run wants the kept handle back each round, the counts steady and the parked slots freed outside", () => {
  const round = { away: "u2 0 0 5", back: "u2 1 0 7", counts: "fua_handles: live 1 parked 2 free 0 retired 0" };
  const run = { first: "u1 7 1 0", rounds: Array.from({ length: ROUNDS }, () => round), outside: "u3 0", outsideCounts: "fua_handles: live 1 parked 0 free 2 retired 0" };
  assert.deepEqual(judgeHub(run), []);
  assert.match(judgeHub({ ...run, rounds: [round, { ...round, back: "u2 0 0 9" }] }).join(), /round 2: back on MAP01/);
  assert.match(judgeHub({ ...run, rounds: [round, { ...round, counts: "fua_handles: live 1 parked 4 free 0 retired 0" }] }).join(), /not round 1's/);
  assert.match(judgeHub({ ...run, outsideCounts: "fua_handles: live 1 parked 2 free 0 retired 0" }).join(), /slots parked/);
  assert.match(judgeHub({ ...run, rounds: [{ ...round, away: "u2 1 0 5", counts: "" }] }).join(), /away on MAP02.*fua_handles printed nothing/);
  assert.match(MAPINFO, /cluster 1\n\{\n\thub\n\}/);
});

test("the netgame wants the server's handle and each client's valid handle to the barrel", () => {
  const ok = { h: 1, ok: 1, barrel: 1 };
  assert.deepEqual(judgeClients(4, { a: ok, b: ok }), []);
  assert.deepEqual(judgeClients(0, { a: { h: 0, ok: 0, barrel: 0 } }), ["the server's own handle is 0", "a took handle 0, valid 0, barrel 0, not a valid handle to the barrel"]);
  assert.ok(sendScript().includes(24020) && sendScript().includes(215));
  assert.equal(clientScript()[0], PCD.PUSHSCRIPTVAR);
});

test("each pk3 holds its maps and lumps", () => {
  assert.ok(buildAcsHandlesPk3().toString("latin1").includes("maps/MAP02.wad"));
  const hub = buildAcsHandlesHubPk3().toString("latin1");
  assert.ok(hub.includes("maps/MAP04.wad") && hub.includes("mapinfo.txt"));
  assert.ok(buildNetHandlesPk3().toString("latin1").includes("user int zx_hok"));
});
