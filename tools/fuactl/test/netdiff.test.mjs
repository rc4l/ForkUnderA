import { test } from "node:test";
import assert from "node:assert/strict";
import { sampleAt, angleApart, diffActors, diffMap, diffLines, readPaged, readWorld, recordServerHistory, netdiff, DEFAULT_TOLERANCE } from "../src/netdiff.mjs";

const actor = (netid, extra = {}) => ({ netid, c: "DoomImp", x: 0, y: 0, z: 0, angle: 90, health: 60, player: -1, ...extra });
const sector = (i, extra = {}) => ({ i, floor: 0, ceiling: 128, light: 160, color: 16777215, fade: 0, desat: 0, floorTex: "F", ceilingTex: "C", ...extra });

test("the history sample for a tic is the latest not after it, or the earliest", () => {
  const h = [{ tic: 10 }, { tic: 12 }, { tic: 15 }];
  assert.equal(sampleAt(h, 13).tic, 12);
  assert.equal(sampleAt(h, 15).tic, 15);
  assert.equal(sampleAt(h, 99).tic, 15);
  assert.equal(sampleAt(h, 5).tic, 10);
  assert.equal(sampleAt([], 5), null);
});

test("angles are compared the short way round", () => {
  assert.equal(angleApart(359, 1), 2);
  assert.equal(angleApart(90, 270), 180);
  assert.equal(angleApart(10, 30), 20);
});

test("actors agree within tolerance and every kind of difference is named", () => {
  assert.deepEqual(diffActors([actor(1)], [actor(1, { x: 0.5, angle: 91 })]), []);
  const d = diffActors([actor(1), actor(2), actor(3, { health: 60 }), actor(4)], [actor(1, { x: 5 }), actor(3, { health: 20, c: "Zombie" }), actor(4, { angle: 180 }), actor(9)]);
  const what = d.map((x) => x.what);
  assert.ok(what.includes("DoomImp only on the server"));
  assert.ok(what.includes("DoomImp only on the client"));
  assert.ok(what.some((w) => /5\.00 units apart/.test(w)));
  assert.ok(what.includes("class DoomImp on the server, Zombie on the client"));
  assert.ok(what.includes("DoomImp health 60 on the server, 20 on the client"));
  assert.ok(what.includes("DoomImp facing 90 on the server, 180 on the client"));
});

test("a skipped actor and positions switched off compare only presence, class and health", () => {
  assert.deepEqual(diffActors([actor(1)], [actor(1, { x: 50 })], { skip: new Set([1]) }), []);
  assert.deepEqual(diffActors([actor(1)], [actor(1, { x: 50 })], { positions: false }), []);
  assert.equal(diffActors([actor(1)], [actor(1, { health: 59 })], { tolerance: { ...DEFAULT_TOLERANCE, health: 1 } }).length, 0);
});

test("sectors and sidedefs differ field by field", () => {
  const server = { sectors: [sector(0), sector(1)], sides: [{ i: 0, top: "A", mid: "-", bottom: "B", light: 0 }] };
  assert.deepEqual(diffMap(server, server), []);
  const client = { sectors: [sector(0, { floor: 0.2 }), sector(1, { ceiling: 64, light: 200, floorTex: "X" })], sides: [{ i: 0, top: "A", mid: "-", bottom: "SW2", light: 0 }] };
  const d = diffMap(server, client).map((x) => x.what);
  assert.deepEqual(d, ["sector 1: ceiling 128/64, light 160/200, floorTex F/X (server/client)", "side 0: bottom B/SW2 (server/client)"]);
  assert.match(diffMap(server, { sectors: [sector(0)], sides: [] })[0].what, /2 sectors on the server, 1 on the client/);
});

test("the verdict lines agree or list each difference", () => {
  assert.deepEqual(diffLines("MAP01", [], { actors: 3, sectors: 9, sides: 30 }), ["AGREE MAP01: 3 actors, 9 sectors, 30 sides"]);
  const many = Array.from({ length: 45 }, (_, i) => ({ what: `d${i}` }));
  const lines = diffLines("MAP01", many, {});
  assert.equal(lines[0], "DIFFER MAP01: 45 difference(s)");
  assert.equal(lines.length, 42);
  assert.equal(lines.at(-1), "  ... 5 more");
});

test("a paged read takes every page", async () => {
  const calls = [];
  const c = { rpc: async (m, a) => { calls.push(a.first); return { total: 5, sectors: Array.from({ length: Math.min(2, 5 - a.first) }, (_, k) => ({ i: a.first + k })) }; } };
  const rows = await readPaged(c, "world.sectorstate", "sectors", { page: 2 });
  assert.deepEqual(rows.map((r) => r.i), [0, 1, 2, 3, 4]);
  assert.deepEqual(calls, [0, 2, 4]);
  assert.deepEqual(await readPaged({ rpc: async () => null }, "world.sectorstate", "sectors"), []);
});

test("a world read gathers net state, actors, sectors and sides", async () => {
  const c = { rpc: async (m) => ({ "net.state": { gametic: 7 }, "world.actors": { tic: 8, actors: [actor(1)] }, "world.sectorstate": { total: 1, sectors: [sector(0)] }, "world.sidestate": { total: 0, sides: [] } })[m] };
  const w = await readWorld(c);
  assert.equal(w.tic, 8);
  assert.equal(w.actors.length, 1);
  assert.equal(w.sectors.length, 1);
  assert.deepEqual((await readWorld(c, { sides: false })).sides, []);
});

test("the server's history keeps one sample a tic", async () => {
  let t = 0, tic = 10;
  const sc = { rpc: async () => ({ tic: Math.floor(tic++ / 2), actors: [] }) };
  const h = await recordServerHistory(sc, { seconds: 0.01, sleep: async () => {}, now: () => (t += 1) });
  assert.deepEqual(h.map((s) => s.tic), [5, 6, 7, 8, 9]);
});

test("netdiff sets the client's actors against the server at its heard tic and leaves its own pawn out", async () => {
  const history = [{ tic: 100, actors: [actor(1, { x: 0 }), actor(5, { c: "DoomPlayer", player: 1 })] }, { tic: 101, actors: [actor(1, { x: 8 }), actor(5, { c: "DoomPlayer", player: 1 })] }];
  const server = { tic: 102, net: { role: "server" }, actors: [actor(1, { x: 16 })], sectors: [sector(0)], sides: [] };
  const client = { tic: 99, net: { serverTic: 100, consoleplayer: 1 }, actors: [actor(1, { x: 0 }), actor(5, { c: "DoomPlayer", player: 1, x: 30 })], sectors: [sector(0)], sides: [] };
  const reads = [server, client];
  const r = await netdiff({}, {}, { deps: { record: async () => history.slice(), read: async () => reads.shift() } });
  assert.deepEqual(r.diffs, []);
  assert.equal(r.alignedTic, 100);
  assert.equal(r.compared.actors, 2);
});
