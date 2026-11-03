// [Doruk-Aksoy] Tests for the decal stress map and its bench's parsing.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { PCD } from "../src/acs.mjs";
import { buildDecalStressWad, routeAt, routeScript, parseSet, summariseCapture, ROUTE_TICS, TURRET, TURRET_TID, TURRETS, TICS } from "../scenarios/decalstress.mjs";

test("the wad carries the turret's DECORATE and one map with its script", () => {
  const lumps = readWad(buildDecalStressWad());
  assert.deepEqual(lumps.map((l) => l.name), ["DECORATE", "MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  const decorate = lumps[0].data.toString("latin1");
  assert.match(decorate, new RegExp(`actor FuaDecalTurret ${TURRET}`));
  assert.match(decorate, /Decal BulletChip/);
  const textmap = lumps[2].data.toString("latin1");
  assert.equal((textmap.match(new RegExp(`type = ${TURRET};`, "g")) || []).length, TURRETS);
  assert.equal((textmap.match(new RegExp(`id = ${TURRET_TID};`, "g")) || []).length, TURRETS);
  assert.equal((textmap.match(/type = 1;/g) || []).length, 1);
});

test("the route is an ENTER script of one place and angle a tic that starts again, and the turrets go by an OPEN script", () => {
  const behavior = readWad(buildDecalStressWad())[3].data;
  const dir = behavior.readInt32LE(4);
  assert.equal(behavior.readInt32LE(dir), 2);
  assert.equal(behavior.readInt32LE(dir + 4), 4001);
  assert.equal(behavior.readInt32LE(dir + 16), 1002);
  const code = routeScript();
  const perTic = code.indexOf(PCD.DELAYDIRECT) + 2;
  assert.equal(code.length, perTic * ROUTE_TICS + 2);
  assert.deepEqual(code.slice(-2), [PCD.GOTO, 8]);
  const first = routeAt(0);
  assert.equal(code[3], Math.round(first.x * 65536));
  assert.equal(code[5], Math.round(first.y * 65536));
  assert.ok(code.indexOf(PCD.SETACTORPOSITION) < code.indexOf(PCD.SETACTORANGLE));
  assert.ok(TICS.churn[1] <= TICS.stop && TICS.stop < TICS.steady[0]);
});

test("the route laps and stays inside the room", () => {
  assert.deepEqual(routeAt(ROUTE_TICS), routeAt(0));
  for (let t = 0; t < ROUTE_TICS; t += 50) {
    const p = routeAt(t);
    assert.ok(p.x > 0 && p.x < 2048 && p.y > 0 && p.y < 2048);
  }
});

test("a set names a binary and launch cvars, either left out", () => {
  assert.deepEqual(parseSet("a/forkundera.exe@fua_decal_cache=0,cl_maxdecals=64"), { engine: "a/forkundera.exe", cvars: { fua_decal_cache: "0", cl_maxdecals: "64" } });
  assert.deepEqual(parseSet("@fua_decal_cache=1"), { engine: undefined, cvars: { fua_decal_cache: "1" } });
  assert.deepEqual(parseSet(""), { engine: undefined, cvars: {} });
});

test("a capture sums up as its frame times and the mean of each column, a missing one null", () => {
  const csv = "frame/ms,scene/decals,scene/decal_sides_kept\n2,0.1,10\n4,0.3,\n3,0.2,20\n";
  const s = summariseCapture(csv);
  assert.equal(s.frames, 3);
  assert.equal(s.frameMs, 3);
  assert.equal(s.p99, 4);
  assert.ok(Math.abs(s["scene/decals"] - 0.2) < 1e-9);
  assert.equal(s["scene/decal_sides_kept"], 10);
  assert.equal(s["scene/decal_sides_derived"], null);
  assert.deepEqual(summariseCapture("frame/ms\n"), { frames: 0, frameMs: 0, p99: 0, "scene/decals": null, "scene/decal_sides_kept": null, "scene/decal_sides_derived": null });
});
