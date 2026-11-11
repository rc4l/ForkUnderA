// [Doruk-Aksoy] The flat model template: its disc, its actors and models, and the room with and without the discs.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad, readPk3, parseUdmf, wad, mapLumps } from "../src/udmf.mjs";
import { buildFlatModelPk3, discObj, flatRoom, DISCS, MONSTER, BOXES, SIDES, VANTAGES } from "../scenarios/flatmodelfeet.mjs";

const files = (pk3) => Object.fromEntries(readPk3(pk3).map((f) => [f.name, f.data]));
const textmap = (all, map) => parseUdmf(readWad(all[`maps/${map}.wad`]).find((l) => l.name === "TEXTMAP").data.toString("latin1"));

test("the disc is one flat square at height 0, with both windings", () => {
  const lines = discObj(96).split("\r\n");
  const ys = lines.filter((l) => l.startsWith("v ")).map((l) => Number(l.split(" ")[2]));
  assert.deepEqual(ys, [0, 0, 0, 0]);
  assert.deepEqual(lines.filter((l) => l.startsWith("f ")), ["f 1/1 2/2 3/3 4/4", "f 4/4 3/3 2/2 1/1"]);
});

test("MAP01 has every disc and MAP02 none, both with the same monster spots and one player start", () => {
  const all = files(buildFlatModelPk3());
  const one = textmap(all, "MAP01"), two = textmap(all, "MAP02");
  for (const d of DISCS) {
    assert.equal(one.thing.filter((t) => t.type === d.num).length, 1, d.name);
    assert.equal(two.thing.filter((t) => t.type === d.num).length, 0, d.name);
  }
  const spots = (t) => t.thing.filter((th) => th.type === MONSTER.num).map((th) => [th.x, th.y, th.height]);
  assert.equal(spots(one).length, 9);
  assert.deepEqual(spots(one), spots(two));
  assert.equal(one.thing.filter((t) => t.type === 1).length, 1);
});

test("the monsters come from a spot at runtime, since a locked launch skips map monsters, and every disc has its model", () => {
  const all = files(buildFlatModelPk3());
  const dec = all["decorate.txt"].toString("latin1"), md = all["modeldef.txt"].toString("latin1");
  assert.match(dec, new RegExp(`Actor ${MONSTER.spot} ${MONSTER.num}[\\s\\S]*A_SpawnItemEx\\("${MONSTER.name}"\\)`));
  assert.doesNotMatch(dec, new RegExp(`Actor ${MONSTER.name} \\d`));
  for (const d of DISCS) {
    assert.match(dec, new RegExp(`Actor ${d.name} ${d.num}[\\s\\S]*?RenderStyle ${d.style}`));
    assert.match(md, new RegExp(`Model ${d.name}[\\s\\S]*?Zoffset ${d.zoffset}`));
  }
  assert.ok(all["models/disc.obj"]);
  const flat = all["fuamodel.txt"].toString("latin1").split("\n").filter((l) => l.startsWith("FlatSurface "));
  assert.deepEqual(flat, DISCS.map((d) => `FlatSurface ${d.name}`));
});

test("a room can start its player elsewhere, and the pk3 takes other maps", () => {
  const m = flatRoom(true, { x: 424, y: 504, angle: 141 });
  const start = m.things.find((t) => t.type === 1);
  assert.deepEqual([start.x, start.y, start.angle], [424, 504, 141]);
  const all = files(buildFlatModelPk3([{ name: "MAP03", data: wad(mapLumps("MAP03", m)) }]));
  assert.ok(all["maps/MAP03.wad"]);
  assert.equal(all["maps/MAP01.wad"], undefined);
});

test("every box names a vantage that exists and is judged against the room without discs", () => {
  const names = new Set(VANTAGES.map((v) => v.name));
  assert.deepEqual(SIDES.map((s) => s.name), ["vk", "vkEmpty"]);
  for (const [name, b] of Object.entries(BOXES.MAP01)) {
    assert.ok(names.has(b.vantage), name);
    assert.deepEqual(b.pair, ["vk", "vkEmpty"], name);
    assert.ok((b.ceiling ?? b.floor) > 0, name);
  }
});
