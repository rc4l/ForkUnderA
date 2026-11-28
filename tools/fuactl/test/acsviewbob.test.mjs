import { test } from "node:test";
import assert from "node:assert/strict";
import { SAMPLE_TICS, BOB_SPAN, STILL_SPAN, SIDES, span, judgeHover, riseAndHover, sampleHover, cameraOf, buildViewBobWad } from "../scenarios/acsviewbob.mjs";
import { CONFIGS, serverPawnOf, judgeServer, judgeNetplay } from "../scenarios/netacsviewbob.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";

// [Doruk-Aksoy] SAMPLE_TICS samples whose height and view swing by `z` and `view`.
const hover = (z, view) => Array.from({ length: SAMPLE_TICS }, (_, i) => ({ z: 64 + (i % 2) * z, view: 41 + (i % 2) * view }));

test("a hover bobs past BOB_SPAN with fviewbob on and stays within STILL_SPAN off", () => {
  assert.equal(span([3, 1, 2]), 2);
  assert.equal(span([]), 0);
  assert.deepEqual(judgeHover(hover(1.6, 1), true), []);
  assert.deepEqual(judgeHover(hover(0, 0), false), []);
  assert.match(judgeHover(hover(0.1, 1), true).join(), /height moved 0.100 with fviewbob on/);
  assert.match(judgeHover(hover(1, 0), true).join(), /view bobbed 0.000 with fviewbob on/);
  assert.match(judgeHover(hover(1.6, 0), false).join(), /height moved 1.600 with fviewbob off/);
  assert.match(judgeHover(hover(0, 1), false).join(), /view bobbed 1.000 with fviewbob off/);
  assert.match(judgeHover(hover(0, 0).slice(1), false).join(), /only 39 samples/);
  assert.ok(BOB_SPAN > STILL_SPAN);
});

test("the player flies, rises a moment, then hovers before a tic-by-tic sample", async () => {
  const sent = [];
  const deps = { exec: async (c, t) => sent.push(t), waitTics: async (c, n) => sent.push(n) };
  await riseAndHover("p", "s", deps);
  assert.deepEqual(sent, ["fly", "+moveup", 12, "-moveup", 140]);
  let n = 0;
  const got = await sampleHover(async () => (n++ % 4 ? { z: n, view: 0 } : null), "s", { waitTics: async () => {} });
  assert.equal(got.length, SAMPLE_TICS - SAMPLE_TICS / 4);
  assert.deepEqual(await cameraOf({ rpc: async () => ({ z: 10, viewz: 51 }) })(), { z: 10, view: 41 });
  assert.equal(await cameraOf({ rpc: async () => { throw new Error("x"); } })(), null);
});

test("the server's pawn is read from world.actors, and only its height judged", async () => {
  const c = { rpc: async () => ({ actors: [{ player: 1, z: 5 }, { player: 0, z: 7 }] }) };
  assert.deepEqual(await serverPawnOf(c)(), { z: 7, view: 0 });
  assert.equal(await serverPawnOf({ rpc: async () => ({ actors: [] }) })(), null);
  assert.deepEqual(judgeServer(hover(1.6, 0), true), []);
  assert.deepEqual(judgeServer(hover(0, 0), false), []);
  assert.match(judgeServer(hover(0, 0), true).join(), /height moved/);
});

test("the netgame judges the server and the client by the client's own fviewbob", async () => {
  const set = { server: { c: "s" }, clients: [{ c: "p" }] };
  let calls = 0;
  const deps = { rise: async () => {}, sample: async () => (calls++ ? hover(0, 0) : hover(0, 0)) };
  assert.equal((await judgeNetplay(set, "MAP01 off", { config: CONFIGS[1] }, deps)).failed, 0);
  const r = await judgeNetplay(set, "MAP01 on", { config: CONFIGS[0] }, deps);
  assert.match(r.line, /the server: the height moved .* with fviewbob on/);
  assert.match(r.line, /the player's client: the height moved/);
  assert.deepEqual(CONFIGS.map((c) => [c.name, c.clientCvars.fviewbob]), [["on", 1], ["off", 0], ["off-ping100", 0]]);
  for (const c of CONFIGS) assert.deepEqual(c.serverCvars, SNAP_SERVER_CVARS);
  assert.deepEqual(SIDES.map((s) => s.launchCvars.fviewbob), [1, 0]);
  assert.ok(buildViewBobWad().length > 0);
});
