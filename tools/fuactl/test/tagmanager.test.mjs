import { test } from "node:test";
import assert from "node:assert/strict";
import { buildMoreIdsMap, buildHexenMap, buildLinelessMap, buildTagManagerPk3, CHECKS, COUNTER, READ, COLUMN } from "../scenarios/tagmanager.mjs";
import { judgeCounter } from "../src/template.mjs";
import { readPk3, readWad, parseUdmf } from "../src/udmf.mjs";
import { binaryMap } from "../src/mapdata.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("each kind of check reads a sector's light by tag, a line's activation by ID, or the column counted by tid or class", () => {
  assert.deepEqual(READ.sector(5), [PCD.PUSHNUMBER, 5, PCD.GETSECTORLIGHTLEVEL]);
  assert.deepEqual(READ.line(7), [PCD.PUSHNUMBER, 7, PCD.CALLFUNC, 1, ACSF.GetLineActivation]);
  assert.deepEqual(READ.count(0), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, COLUMN.tid, PCD.PUSHNUMBER, 0, PCD.THINGCOUNTSECTOR]);
  assert.deepEqual(READ.countname(3), [PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 3, PCD.THINGCOUNTNAMESECTOR]);
  assert.ok(Object.values(CHECKS).flat().every(([, kind]) => READ[kind]));
});

test("a failing map's counter names its checks", () => {
  assert.match(judgeCounter("MAP02", `"zx_tagmanager_wrong" is "3"`, COUNTER).line, /^FAIL MAP02 .*: Line_SetIdentification/);
  assert.equal(judgeCounter("MAP04", `"zx_tagmanager_wrong" is "1"`, COUNTER).failed, 0);
});

test("the UDMF map tags sector 0 and line 0 and gives a sector and a line moreids", () => {
  const u = parseUdmf(buildMoreIdsMap().textmap());
  assert.equal(u.sector[0].id, 3);
  assert.deepEqual([u.sector[2].id, u.sector[2].moreids], [4, "5 6"]);
  assert.deepEqual([u.linedef[0].id, u.linedef[0].impact], [7, true]);
  assert.deepEqual([u.linedef[1].id, u.linedef[1].moreids, u.linedef[1].playerpush], [8, "9 10", true]);
  // [rc4l] The column stands inside sector 1's block, the only untagged sector.
  const column = u.thing.find((t) => t.id === COLUMN.tid);
  assert.equal(column.type, COLUMN.type);
  assert.ok(column.x > 64 && column.x < 256 && column.y > 64 && column.y < 256 && u.sector[1].id === undefined);
});

test("the Hexen map's line 1 sets ID 263 and is the one used by pressing", () => {
  const pk = new Map(readPk3(buildTagManagerPk3()).map((f) => [f.name, f.data]));
  const map = binaryMap(new Map(readWad(pk.get("maps/MAP02.wad")).map((l) => [l.name, l.data])));
  assert.equal(map.namespace, "hexen (binary)");
  assert.deepEqual([map.linedef[1].special, map.linedef[1].arg0, map.linedef[1].arg4, map.linedef[1].flags >> 10], [121, 7, 1, 1]);
  assert.ok([0, 2, 3].every((i) => map.linedef[i].special === 0 && (map.linedef[i].flags >> 10) === 0));
  assert.equal(map.sector[0].id, 3);
  assert.equal(buildHexenMap().lines.length, 4);
});

test("the line-less maps have a sector no side faces, tagged or not", () => {
  for (const tagged of [false, true]) {
    const m = buildLinelessMap(tagged);
    assert.equal(m.sectors.length, 2);
    assert.ok(m.sides.every((s) => s.sector === 0));
    assert.equal(m.sectors[0].id, tagged ? 3 : undefined);
  }
});
