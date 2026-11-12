import { test } from "node:test";
import assert from "node:assert/strict";
import { buildMap, buildUserKeysPk3, MAPS, READ, KEY, COUNTER } from "../scenarios/udmfuserkeys.mjs";
import { parseUdmf, readPk3 } from "../src/udmf.mjs";
import { judgeCounter } from "../src/template.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("the base-namespace map and the ZDoom one carry the same user key on the room, its west wall and that wall's side", () => {
  for (const namespace of ["doom", "zdoom"]) {
    const u = parseUdmf(buildMap(namespace).textmap());
    assert.equal(u.namespace, namespace);
    assert.equal(u.sector[0][KEY], 7);
    assert.equal(u.linedef[0][KEY], 7);
    assert.equal(u.sidedef[u.linedef[0].sidefront][KEY], 7);
    assert.ok(u.linedef.slice(1).every((l) => l[KEY] === undefined));
  }
  assert.deepEqual(Object.values(MAPS).map((m) => m.checks.map((c) => c[3])), [[0, 0, 0], [7, 7, 7]]);
  assert.deepEqual(readPk3(buildUserKeysPk3()).map((f) => f.name), ["maps/MAP01.wad", "maps/MAP02.wad", "mapinfo.txt", "cvarinfo.txt"]);
});

test("the checks read the key through the UDMF functions by tag, line ID and front side", () => {
  assert.deepEqual(READ.sector(5), [PCD.PUSHNUMBER, 5, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.GetSectorUDMFInt]);
  assert.deepEqual(READ.line(6), [PCD.PUSHNUMBER, 6, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.GetLineUDMFInt]);
  assert.deepEqual(READ.side(6), [PCD.PUSHNUMBER, 6, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 3, ACSF.GetSideUDMFInt]);
  assert.match(judgeCounter("MAP01", `"zx_udmfuserkeys_wrong" is "15"`, COUNTER).line, /: the sector's; the line's; the side's$/);
});
