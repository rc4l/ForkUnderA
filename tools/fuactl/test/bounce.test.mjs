// [Doruk-Aksoy] The bouncing projectile templates (bounce.mjs, netbounce.mjs): their maps, their DECORATE against the engine's flags, and the netgame judge.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad, readPk3, parseUdmf } from "../src/udmf.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { engineFlagNames, decorateFaults } from "../src/decorate.mjs";
import * as bounce from "../scenarios/bounce.mjs";
import * as net from "../scenarios/netbounce.mjs";

const files = (pk3) => Object.fromEntries(readPk3(pk3).map((f) => [f.name, f.data]));
const lump = (all, map, name) => readWad(all[`maps/${map}.wad`]).find((l) => l.name === name).data;
const textmap = (all, map) => parseUdmf(lump(all, map, "TEXTMAP").toString("latin1"));

test("the engine's flag table names the flags the templates use, and not MBF's PASSMOBJ spelling", () => {
  const flags = engineFlagNames();
  for (const f of ["SOLID", "CANPASS", "BOUNCEONWALLS", "BOUNCEONFLOORS", "BOUNCEONCEILINGS", "NOGRAVITY", "FIREDAMAGE"]) assert.ok(flags.has(f), f);
  assert.ok(!flags.has("PASSMOBJ"));
});

test("a DECORATE check names an unknown flag and a state block on one line, and passes a clean one", () => {
  const flags = new Set(["SOLID"]);
  assert.deepEqual(decorateFaults("Actor A\n{\n  +SOLID\n  -Inventory.Solid\n}\n", flags), []);
  assert.deepEqual(decorateFaults("  +PASSMOBJ\n", flags), ["line 1: unknown flag PASSMOBJ"]);
  assert.deepEqual(decorateFaults("  States { Spawn: COLU A -1 Stop }\n", flags), ["line 1: a state block on one line, which reads its Stop as a frame parameter"]);
});

test("both templates' DECORATE passes the engine's flag table", () => {
  assert.deepEqual(decorateFaults(bounce.DECORATE), []);
  assert.deepEqual(decorateFaults(net.DECORATE), []);
});

test("bounce.mjs packs each map with its scripts, the DECORATE, MAPINFO and the counter", () => {
  const all = files(bounce.buildBouncePk3());
  for (const map of Object.keys(bounce.MAPS)) assert.ok(lump(all, map, "BEHAVIOR").length > 0, map);
  assert.equal(all["decorate.txt"].toString(), bounce.DECORATE);
  assert.match(all["cvarinfo.txt"].toString(), new RegExp(`server int ${bounce.COUNTER_CVAR} = 0;`));
  assert.match(all["mapinfo.txt"].toString(), /map MAP05 "MBF spent"/);
});

test("each map holds a spot per actor it spawns, at that actor's height, and one player start", () => {
  const all = files(bounce.buildBouncePk3());
  for (const [map, { spawn }] of Object.entries(bounce.MAPS)) {
    const t = textmap(all, map);
    const spots = t.thing.filter((th) => th.type === THING.MapSpot);
    assert.deepEqual(spots.map((s) => [s.x, s.y, s.height, s.id]), spawn.map(([, x, y, z], i) => [x, y, z, 10 + i]), map);
    assert.equal(t.thing.filter((th) => th.type === 1).length, 1, map);
  }
});

// [Doruk-Aksoy] A line's right-hand normal, which a one-sided wall must point into the room with.
const rightOf = (t, l) => { const a = t.vertex[l.v1], b = t.vertex[l.v2]; return [b.y - a.y, a.x - b.x]; };

test("MAP01's pillar faces look out into the room, beside a divider the path's box crosses, listed after the pillar in one lane and before it in the other", () => {
  const t = textmap(files(bounce.buildBouncePk3(["MAP01"])), "MAP01");
  const { x0, y0, x1, y1 } = bounce.PILLAR;
  for (const dy of [0, bounce.LANE_DY]) {
    const inPillar = (l) => [t.vertex[l.v1], t.vertex[l.v2]].every((v) => v.x >= x0 && v.x <= x1 && v.y >= dy + y0 && v.y <= dy + y1);
    const faces = t.linedef.map((l, i) => ({ l, i })).filter(({ l }) => inPillar(l));
    assert.equal(faces.length, 4, `lane ${dy}`);
    for (const { l } of faces) {
      assert.ok(!l.twosided, `lane ${dy}`);
      const [nx, ny] = rightOf(t, l), a = t.vertex[l.v1];
      const out = (a.x - (x0 + x1) / 2) * nx + (a.y - (dy + (y0 + y1) / 2)) * ny;
      assert.ok(out > 0, `lane ${dy}: a face looks into the pillar`);
    }
    const divider = t.linedef.findIndex((l) => l.twosided && t.vertex[l.v1].y === dy + bounce.DIVIDER_Y && t.vertex[l.v2].y === dy + bounce.DIVIDER_Y);
    assert.ok(divider >= 0, `lane ${dy}`);
    const after = faces.every(({ i }) => i < divider);
    assert.equal(after, dy === 0, `lane ${dy}: the divider's place in the line list`);
    // [Doruk-Aksoy] Low enough to narrow the box, high enough to clear the missile at 64 + 8.
    assert.equal(t.sector[t.sidedef[t.linedef[divider].sidefront].sector].heightceiling, 128);
  }
  const [, , py] = bounce.MAPS.MAP01.spawn[0];
  assert.ok(py - 8 < bounce.DIVIDER_Y && py + 8 > bounce.DIVIDER_Y, "the box straddles the divider");
  assert.ok(py + 8 > y0 && py - 8 < y0, "the box's top overlaps the pillar, its leading bottom corner does not");
});

test("MAP04's east floor is sloped by Plane_Align on its own front", () => {
  const t = textmap(files(bounce.buildBouncePk3(["MAP04"])), "MAP04");
  const align = t.linedef.filter((l) => l.special === SPECIAL.Plane_Align);
  assert.equal(align.length, 1);
  assert.equal(align[0].arg0, 1);
  assert.equal(t.sector[t.sidedef[align[0].sidefront].sector].heightfloor, 128);
});

test("netbounce.mjs: MAP01's post is spawned by a CLIENTSIDE script, MAP02's by the server inside a corridor 16 high", () => {
  const all = files(net.buildNetBouncePk3());
  const map1 = lump(all, "MAP01", "BEHAVIOR"), map2 = lump(all, "MAP02", "BEHAVIOR");
  assert.equal(map1.subarray(0, 4).toString("latin1"), "ACSE");
  assert.ok(map1.includes(Buffer.from("SFLG")) && map1.includes(Buffer.from("ZXNetPost")));
  assert.ok(map2.includes(Buffer.from("ZXNetPost")));
  const sfl = map2.indexOf(Buffer.from("SFLG"));
  assert.equal(map2.readUInt32LE(sfl + 4), 0, "MAP02 flags no script");
  const t = textmap(all, "MAP02");
  assert.deepEqual(t.sector.map((s) => s.heightceiling), [256, 16]);
  const [, my] = net.MAPS.MAP02.missile, [, py] = net.MAPS.MAP02.post;
  assert.ok(Math.abs(my - py) > 16 && Math.abs(my - py) < 22, "the bouncer meets the post off-centre");
});

// [Doruk-Aksoy] A server flying east along y 0 at 8 a tic from x 0, then back west from x 40, as world.actors rows.
const B = "ZXNetBouncer";
const flight = [0, 8, 16, 24, 32, 40, 32, 24].map((x, i) => ({ tic: i, actors: [{ c: B, x, y: 0, z: 0, angle: i < 6 ? 0 : 180 }] }));
const at = (x, angle, v = 8) => ({ c: B, x, y: 0, z: 0, angle, vx: angle === 0 ? v : -v, vy: 0, vz: 0 });

test("a client on the server's path, facing its way there, follows it, even where the path turns back over itself", () => {
  assert.deepEqual(net.judgeFollow(flight, [at(8, 0), at(16, 0)], B), []);
  assert.deepEqual(net.judgeFollow(flight, [at(32, 180), at(24, 180)], B), []);
});

test("a client a tic past the server's turn, the tic before the server's word, is still on its path", () => {
  assert.deepEqual(net.judgeFollow(flight, [at(40, 0), at(48, 0)], B), []);
  assert.match(net.judgeFollow(flight, [at(40, 0), at(60, 0)], B)[0], /units off the server's path/);
});

test("a client whose copy stopped, left the path, faces the other way or holds none is named for it", () => {
  assert.match(net.judgeFollow(flight, [at(16, 0), at(16, 0)], B)[0], /stopped at \(16\.0, 0\.0\)/);
  assert.match(net.judgeFollow(flight, [{ ...at(8, 0), y: 30 }, at(16, 0)], B)[0], /30\.00 units off/);
  assert.match(net.judgeFollow(flight, [at(8, 90), at(16, 0)], B)[0], /faces 90 where the server's faced 0/);
  assert.deepEqual(net.judgeFollow(flight, [undefined, at(16, 0)], B), ["holds no bouncer"]);
  assert.deepEqual(net.judgeFollow([{ tic: 0, actors: [] }], [at(8, 0), at(16, 0)], B), ["the server held no bouncer while it was read"]);
});

test("the server's recording keeps every new tic once until it is stopped", async () => {
  const tics = [1, 1, 2, 3];
  let n = 0;
  const sc = { rpc: async () => ({ tic: tics[Math.min(n++, tics.length - 1)], actors: [{ c: B }] }) };
  let release;
  const gate = new Promise((r) => { release = r; });
  const sleep = async (ms) => { if (ms === 500) { await gate; } else await new Promise((r) => setImmediate(r)); };
  const rec = net.recordUntilStopped(sc, { sleep });
  const stopping = rec.stop();
  while (n < 6) await new Promise((r) => setImmediate(r));
  release();
  const history = await stopping;
  assert.deepEqual(history.map((h) => h.tic), [1, 2, 3]);
});
