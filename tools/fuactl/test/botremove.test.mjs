import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScript, buildBotRemovePk3, judgeCase, BOTINFO, CASES, STRINGS, BOT_INDEX } from "../scenarios/botremove.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("a script calls RemoveBot with its own arguments", () => {
  assert.deepEqual(buildScript([2]), [PCD.PUSHNUMBER, 2, PCD.CALLFUNC, 1, ACSF.RemoveBot, PCD.DROP, PCD.TERMINATE]);
  assert.deepEqual(buildScript([0, 1]), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.RemoveBot, PCD.DROP, PCD.TERMINATE]);
});

test("the cases pass the plain, empty and escape-coloured names, and indices only the bot's takes out", () => {
  assert.deepEqual(STRINGS, ["ZXBot", "", "\x1cgZX\x1cdBot"]);
  assert.deepEqual(CASES.map((k) => k.script), [1, 2, 3, 4, 5, 6, 7, 8]);
  for (const k of CASES.filter((c) => c.args.length > 1)) assert.equal(!k.stays, k.args[1] === BOT_INDEX, k.what);
});

test("a case passes when its bot joined and left", () => {
  assert.deepEqual(judgeCase("an empty name", 1, 0), []);
  assert.deepEqual(judgeCase("an empty name", 1, 1), ["RemoveBot given an empty name left 1 bots"]);
  assert.deepEqual(judgeCase("an empty name", 0, 0), ["addbot left 0 bots, not 1, before an empty name"]);
  assert.deepEqual(judgeCase("another index", 1, 1, true), []);
  assert.deepEqual(judgeCase("another index", 1, 0, true), ["RemoveBot given another index took the bot out"]);
});

test("the bot's BOTINFO name carries colour codes, and the pk3 holds it", () => {
  assert.match(BOTINFO, /name = "\\cgZX\\cdBot"/);
  const bytes = buildBotRemovePk3().toString("latin1");
  assert.ok(bytes.includes("botinfo.txt") && bytes.includes("ZXBot") && bytes.includes("\x1cgZX\x1cdBot"));
});
