import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeVoteMenus, voteEntries, VOTEINFO, MENUDEF, VOTE_LABEL, VOTE_MENU } from "../scenarios/netcallvote.mjs";

const menu = (n) => ({ lines: [{ text: "CALL VOTE" }, { text: "Kick a player" }, ...Array.from({ length: n }, () => ({ text: VOTE_LABEL }))] });

test("the vote's entry is counted by its label alone", () => {
  assert.equal(voteEntries(menu(2)), 2);
  assert.equal(voteEntries({ lines: [{ text: "ZX VOTE MENU" }] }), 0);
  assert.equal(voteEntries(undefined), 0);
});

test("the call vote menu passes listing the vote once both times", () => {
  assert.deepEqual(judgeVoteMenus(menu(1), menu(1)), []);
  assert.deepEqual(judgeVoteMenus(menu(1), menu(2)), ["the call vote menu listed the vote 2 times when opened again"]);
  assert.deepEqual(judgeVoteMenus(menu(0), menu(1)), ["the call vote menu listed the vote 0 times when first opened"]);
});

test("the vote names its submenu, which MENUDEF declares", () => {
  assert.match(VOTEINFO, new RegExp(`Menu = ${VOTE_MENU}, "${VOTE_LABEL}"`));
  assert.match(MENUDEF, new RegExp(`OptionMenu ${VOTE_MENU}`));
});
