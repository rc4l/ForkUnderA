import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScript, buildBotRemovePk3, judgeCase, BOTINFO, CASES, REMOVE_BOT } from "../scenarios/botremove.mjs";
import { PCD } from "../src/acs.mjs";

test("a script calls RemoveBot with its own string", () => {
  assert.deepEqual(buildScript(2), [PCD.PUSHNUMBER, 2, PCD.CALLFUNC, 1, REMOVE_BOT, PCD.DROP, PCD.TERMINATE]);
});

test("the cases pass the plain, empty and escape-coloured names", () => {
  assert.deepEqual(CASES.map((k) => k.text), ["ZXBot", "", "\x1cgZX\x1cdBot"]);
  assert.deepEqual(CASES.map((k) => k.script), [1, 2, 3]);
});

test("a case passes when its bot joined and left", () => {
  assert.deepEqual(judgeCase("an empty name", 1, 0), []);
  assert.deepEqual(judgeCase("an empty name", 1, 1), ["RemoveBot given an empty name left 1 bots"]);
  assert.deepEqual(judgeCase("an empty name", 0, 0), ["addbot left 0 bots, not 1, before an empty name"]);
});

test("the bot's BOTINFO name carries colour codes, and the pk3 holds it", () => {
  assert.match(BOTINFO, /name = "\\cgZX\\cdBot"/);
  const bytes = buildBotRemovePk3().toString("latin1");
  assert.ok(bytes.includes("botinfo.txt") && bytes.includes("ZXBot") && bytes.includes("\x1cgZX\x1cdBot"));
});
