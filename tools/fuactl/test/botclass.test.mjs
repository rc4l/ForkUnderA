import { test } from "node:test";
import assert from "node:assert/strict";
import { judgePawns, buildBotClassPk3, MAPINFO, BOTINFO, CLASS } from "../scenarios/botclass.mjs";

test("the bot's pawn passes as its class and names any other", () => {
  assert.deepEqual(judgePawns([{ player: 0, c: "DoomPlayer" }, { player: 1, c: CLASS }]), []);
  assert.deepEqual(judgePawns([{ player: 0, c: "DoomPlayer" }, { player: 1, c: "DoomPlayer" }]), [`the bot's pawn is DoomPlayer, not ${CLASS}`]);
  assert.deepEqual(judgePawns([{ player: 0, c: "DoomPlayer" }]), ["0 bot pawns, not 1"]);
});

test("the bot's class is the second player class, and the pk3 holds every lump", () => {
  assert.match(MAPINFO, new RegExp(`playerclasses = "DoomPlayer", "${CLASS}"`));
  assert.match(BOTINFO, new RegExp(`class = "${CLASS}"`));
  const text = buildBotClassPk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "decorate.txt", "mapinfo.txt", "botinfo.txt"]) assert.ok(text.includes(name), name);
});
