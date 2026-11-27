import { test } from "node:test";
import assert from "node:assert/strict";
import { buildClosetsMap, buildClosetsWad, closetsBehavior, countIn, cvarOf, CLOSETS, COUNTERS, HOLD_TICS, RUN_SCRIPT, SIDES, TAG, WAKE_TICS, COUNT_AFTER_TICS } from "../scenarios/chaseclosets.mjs";
import { judgeCounter } from "../src/template.mjs";
import { parseUdmf } from "../src/udmf.mjs";
import { PCD } from "../src/acs.mjs";

test("each closet holds its monsters under its own tid, in its own tagged sector", () => {
  const u = parseUdmf(buildClosetsMap().textmap());
  for (const closet of CLOSETS) assert.ok(u.thing.filter((t) => t.id === closet.tid).length > 0, closet.name);
  assert.equal(u.sector.filter((s) => s.id === TAG.closet).length, CLOSETS.length);
  assert.equal(u.sector.filter((s) => s.id === TAG.door).length, CLOSETS.length);
  // [Doruk-Aksoy] No two monsters share a spot, or a closet would start with a stuck pair.
  const spots = u.thing.filter((t) => t.id).map((t) => `${t.x},${t.y}`);
  assert.equal(new Set(spots).size, spots.length);
});

test("the side pukes the run after its notarget, and the count runs after the wake and reads each closet and its door", () => {
  assert.deepEqual(SIDES[0].commands, ["notarget", `puke ${RUN_SCRIPT}`]);
  assert.deepEqual(countIn(101, TAG.closet), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 101, PCD.PUSHNUMBER, TAG.closet, PCD.THINGCOUNTSECTOR]);
  assert.ok(HOLD_TICS > WAKE_TICS + COUNT_AFTER_TICS);
  const lump = closetsBehavior();
  for (const closet of CLOSETS) assert.ok(lump.includes(Buffer.from(cvarOf(closet), "latin1")), closet.name);
});

test("the wad carries the map and a counter per closet that reads -1 until the count runs", () => {
  const text = buildClosetsWad().toString("latin1");
  for (const closet of CLOSETS) assert.match(text, new RegExp(`server int ${cvarOf(closet)} = -1;`));
  assert.equal(COUNTERS.length, CLOSETS.length);
});

test("a closet over its ceiling fails by name, and one never counted fails too", () => {
  const imps = COUNTERS[0];
  assert.equal(judgeCounter("MAP01", `"zx_closet_imps" is "0"`, imps).failed, 0);
  assert.match(judgeCounter("MAP01", `"zx_closet_imps" is "${imps.ceiling + 1}"`, imps).line, /FAIL MAP01 imps still in their closet/);
  assert.equal(judgeCounter("MAP01", `"zx_closet_imps" is "-1"`, imps).failed, 1);
});
