import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeTalk, buildFriendTalkPk3, DIALOG, CHOICE, DIALOGUE, DECORATE, FRIEND, SIDES } from "../scenarios/friendtalk.mjs";
import { readWad, readPk3 } from "../src/udmf.mjs";

test("the screen passes with the friend's line and choice drawn", () => {
  assert.deepEqual(judgeTalk([{ text: DIALOG }, { text: CHOICE }]), []);
  assert.deepEqual(judgeTalk([{ text: "100%" }]), ["using the friend opened no conversation (100%)"]);
  assert.deepEqual(judgeTalk(undefined), ["using the friend opened no conversation ()"]);
});

test("the map carries the friend's DIALOGUE, and DECORATE makes it FRIENDLY", () => {
  assert.match(DIALOGUE, new RegExp(`actor = "${FRIEND}";`));
  assert.match(DECORATE, /\+FRIENDLY/);
  const map = readPk3(buildFriendTalkPk3()).find((f) => f.name === "maps/MAP01.wad");
  assert.deepEqual(readWad(map.data).map((l) => l.name), ["MAP01", "TEXTMAP", "DIALOGUE", "ENDMAP"]);
  assert.deepEqual(SIDES.map((s) => s.launchCvars.sv_shootthroughallies), [0, 1]);
});
