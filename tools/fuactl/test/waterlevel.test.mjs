import { test } from "node:test";
import assert from "node:assert/strict";
import { buildWaterMap, buildWaterLevelPk3, BARRELS, CHECKS, COUNTER, POOLS, READ, TAG, APROP_WATERLEVEL } from "../scenarios/waterlevel.mjs";
import { judgeCounter } from "../src/template.mjs";
import { readPk3, parseUdmf } from "../src/udmf.mjs";
import { PCD } from "../src/acs.mjs";
import { THING, SPECIAL } from "../src/doomnums.mjs";

test("each check reads a barrel's waterlevel by tid", () => {
  assert.deepEqual(READ.water(4), [PCD.PUSHNUMBER, 4, PCD.PUSHNUMBER, APROP_WATERLEVEL, PCD.GETACTORPROPERTY]);
  assert.equal(CHECKS.length, BARRELS.length);
  assert.ok(CHECKS.every(([, kind]) => READ[kind]));
});

test("a failing check is named", () => {
  assert.match(judgeCounter("MAP01", `"zx_waterlevel_wrong" is "${1 + 2 ** 6}"`, COUNTER).line, /non-swimmable Boom water keeps 0 \(the reset\)/);
  assert.equal(judgeCounter("MAP01", `"zx_waterlevel_wrong" is "1"`, COUNTER).failed, 0);
});

test("the map has every kind of water, a control line per tag, and each barrel in its pool", () => {
  const u = parseUdmf(buildWaterMap().textmap());
  assert.ok(u.sector.some((s) => s.waterzone === true));
  const lines = u.linedef.filter((l) => l.special);
  assert.deepEqual(lines.map((l) => [l.special, l.arg0, l.arg1]), [
    [SPECIAL.Transfer_Heights, TAG.swim, 8], [SPECIAL.Transfer_Heights, TAG.still, 0],
    [SPECIAL.Sector_Set3DFloor, TAG.deep3d, 2], [SPECIAL.Sector_Set3DFloor, TAG.shallow3d, 2]]);
  for (const [, tid, pool, height] of BARRELS) {
    const t = u.thing.find((x) => x.id === tid);
    assert.equal(t.type, THING.ExplosiveBarrel);
    assert.equal(t.height || 0, height);
    if (pool === null) continue;
    const [, x0, y0] = POOLS.find(([name]) => name === pool);
    assert.ok(t.x > x0 && t.x < x0 + 256 && t.y > y0 && t.y < y0 + 256, `barrel ${tid} in ${pool}`);
  }
  // [Doruk-Aksoy] No two barrels of a pool share a spot.
  const spots = u.thing.filter((t) => t.type === THING.ExplosiveBarrel).map((t) => `${t.x},${t.y}`);
  assert.equal(new Set(spots).size, spots.length);
});

test("the pk3 carries the map, its puke, the counter's cvar and the water flat's splash", () => {
  const files = readPk3(buildWaterLevelPk3());
  assert.deepEqual(files.map((f) => f.name), ["maps/MAP01.wad", "mapinfo.txt", "cvarinfo.txt", "terrain.txt"]);
  assert.match(String(files[3].data), /floor FWATER1 Water/);
});
