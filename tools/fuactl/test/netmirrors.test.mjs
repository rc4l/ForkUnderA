// [Doruk-Aksoy] The mirror listing scenario's verdicts: a server id a client never lists, an id listed twice, and the two levels it runs at.
import { test } from "node:test";
import assert from "node:assert/strict";
import { missingIds, duplicateIds, mirrorVerdict, buildMirrorsWad, CONFIGS } from "../scenarios/netmirrors.mjs";

const row = (netid, extra = {}) => ({ netid, player: -1, ...extra });

test("a server id the client never lists is missing, players and id 0 aside", () => {
  const server = [row(65), row(66), row(67), row(1, { player: 0 }), row(0)];
  assert.deepEqual(missingIds(server, [row(65), row(67)]), [66]);
  assert.deepEqual(missingIds(server, [row(65, { mirror: 1 }), row(66), row(67, { mirror: 1 })], (a) => a.mirror === 1), [66]);
});

test("an id listed twice is a duplicate", () => {
  assert.deepEqual(duplicateIds([row(65), row(66), row(65)]), [65]);
  assert.deepEqual(duplicateIds([row(65), row(66)]), []);
});

test("every id listed passes, a missing one or a duplicate fails by name", () => {
  assert.deepEqual(mirrorVerdict("MAP01 level3 player player 0", { missing: [], duplicates: [], what: "as a mirror" }),
    { line: "PASS MAP01 level3 player player 0: every server actor listed as a mirror", failed: 0 });
  // [Doruk-Aksoy] Seen 2026-11-26 on main: a level 3 client listed none of its 163 mirrors.
  assert.deepEqual(mirrorVerdict("x", { missing: [65, 66], duplicates: [], what: "as a mirror" }),
    { line: "FAIL x: 2 of the server's actors not listed as a mirror (65, 66)", failed: 1 });
  assert.match(mirrorVerdict("x", { missing: [], duplicates: [70], what: "as a mirror" }).line, /ids listed twice by default \(70\)/);
});

test("barrels and deaf zombies in one room, read at levels 3 and 2", () => {
  const text = buildMirrorsWad().toString("latin1");
  assert.equal(text.split("type = 2035;").length - 1, 4);
  assert.equal(text.split("ambush = true;").length - 1, 2);
  assert.deepEqual(CONFIGS.map((c) => [c.name, c.level, c.serverCvars.fua_snap_nolegacy]), [["level3", 3, 3], ["level2", 2, 2]]);
});
