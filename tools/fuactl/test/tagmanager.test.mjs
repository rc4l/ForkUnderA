import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScript, buildMoreIdsMap, buildHexenMap, buildLinelessMap, buildTagManagerPk3, failedChecks, CHECKS, COUNTER, SPAC } from "../scenarios/tagmanager.mjs";
import { judgeCounter } from "../src/template.mjs";
import { readPk3, readWad, parseUdmf } from "../src/udmf.mjs";
import { binaryMap } from "../src/mapdata.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("each check reads a sector by tag or a line by ID and adds its own bit when the value is wrong", () => {
  const words = buildScript([["a", "sector", 5, 128], ["b", "line", 7, SPAC.Impact]]);
  const at = (seq) => words.findIndex((_, i) => seq.every((w, j) => words[i + j] === w));
  assert.ok(at([PCD.PUSHNUMBER, 5, PCD.GETSECTORLIGHTLEVEL, PCD.PUSHNUMBER, 128, PCD.EQ, PCD.IFGOTO]) >= 0);
  assert.ok(at([PCD.PUSHNUMBER, 7, PCD.CALLFUNC, 1, ACSF.GetLineActivation, PCD.PUSHNUMBER, SPAC.Impact, PCD.EQ]) >= 0);
  assert.ok(at([PCD.PUSHNUMBER, 2]) >= 0 && at([PCD.PUSHNUMBER, 4]) >= 0);
  assert.equal(words.at(-1), PCD.TERMINATE);
});

test("a failing counter names the checks whose bits it carries", () => {
  assert.deepEqual(failedChecks("MAP01", 1 + 2 + 16), [CHECKS.MAP01[0][0], CHECKS.MAP01[3][0]]);
  assert.match(judgeCounter("MAP02", `"zx_tagmanager_wrong" is "3"`, COUNTER).line, /^FAIL MAP02 .*: Line_SetIdentification/);
  assert.match(judgeCounter("MAP03", `"zx_tagmanager_wrong" is "0"`, COUNTER).line, /the puke never ran$/);
  assert.equal(judgeCounter("MAP04", `"zx_tagmanager_wrong" is "1"`, COUNTER).failed, 0);
});

test("the UDMF map tags sector 0 and line 0 and gives a sector and a line moreids", () => {
  const u = parseUdmf(buildMoreIdsMap().textmap());
  assert.equal(u.sector[0].id, 3);
  assert.deepEqual([u.sector[2].id, u.sector[2].moreids], [4, "5 6"]);
  assert.deepEqual([u.linedef[0].id, u.linedef[0].impact], [7, true]);
  assert.deepEqual([u.linedef[1].id, u.linedef[1].moreids, u.linedef[1].playerpush], [8, "9 10", true]);
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
