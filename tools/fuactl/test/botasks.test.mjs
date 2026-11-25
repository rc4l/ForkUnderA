import { test } from "node:test";
import assert from "node:assert/strict";
import { buildBotAsksPk3, BOT, BOTINFO, ADD_SCRIPT, MAX_ASKS } from "../scenarios/botasks.mjs";

test("the bot has the stock script, a colour by name and no chat", () => {
  assert.match(BOTINFO, new RegExp(`name = "${BOT}"`));
  assert.match(BOTINFO, /script = "humanbot"/);
  assert.match(BOTINFO, /color = "dark green"/);
  assert.match(BOTINFO, /chatfrequency = 0/);
  const bytes = buildBotAsksPk3().toString("latin1");
  assert.ok(bytes.includes("botinfo.txt") && bytes.includes("maps/MAP01.wad") && bytes.includes(BOT));
  assert.equal(ADD_SCRIPT, 1);
});

test("a join may ask for the script and the colour table, no more", () => {
  assert.equal(MAX_ASKS, 2);
});
