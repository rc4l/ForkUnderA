import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScript, buildBotRemovePk3, BOTINFO, REMOVE_BOT } from "../scenarios/botremove.mjs";
import { PCD } from "../src/acs.mjs";

test("the script calls RemoveBot with the plain name, string 0", () => {
  assert.deepEqual(buildScript(), [PCD.PUSHNUMBER, 0, PCD.CALLFUNC, 1, REMOVE_BOT, PCD.DROP, PCD.TERMINATE]);
});

test("the bot's BOTINFO name carries colour codes, and the pk3 holds it", () => {
  assert.match(BOTINFO, /name = "\\cgZX\\cdBot"/);
  const bytes = buildBotRemovePk3().toString("latin1");
  assert.ok(bytes.includes("botinfo.txt") && bytes.includes("ZXBot"));
});
