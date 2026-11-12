import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeMapLists, showsUsed, buildScript, ENTRY } from "../scenarios/netmaprotation.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

const list = (used) => `Map rotation list: \n\x1cK1. (Current) MAP01 - one\n2. MAP02 - two\n${used ? "\x1cG3. (Used) " : "3. "}MAP03 - three\n`;

test("a maplist shows a map used only with its (Used) mark", () => {
  assert.equal(showsUsed(list(true), "MAP03"), true);
  assert.equal(showsUsed(list(false), "MAP03"), false);
});

test("the script's map passes used on both sides only after the script", () => {
  const ok = { result: 1, serverBefore: list(false), clientBefore: list(false), serverAfter: list(true), clientAfter: list(true) };
  assert.deepEqual(judgeMapLists(ok), []);
  assert.deepEqual(judgeMapLists({ ...ok, clientAfter: list(false) }), ["the client's maplist does not show MAP03 used"]);
  assert.deepEqual(judgeMapLists({ ...ok, result: 0, serverAfter: list(false) }), ["SetMapUsedStatus returned 0, not 1", "the server's maplist does not show MAP03 used"]);
  assert.deepEqual(judgeMapLists({ ...ok, clientBefore: "The map rotation list is empty." }), ["the client's rotation has no MAP03"]);
  assert.deepEqual(judgeMapLists({ ...ok, serverBefore: list(true) }), ["MAP03 was used before the script"]);
});

test("the script marks its entry through SetMapUsedStatus", () => {
  const code = buildScript();
  const at = code.indexOf(ACSF.SetMapUsedStatus);
  assert.deepEqual(code.slice(at - 6, at + 1), [PCD.PUSHNUMBER, ENTRY, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.SetMapUsedStatus]);
});
