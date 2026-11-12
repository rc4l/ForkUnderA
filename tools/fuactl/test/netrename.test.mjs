import { test } from "node:test";
import assert from "node:assert/strict";
import { renameMessage, judgeMessage, buildNetRenamePk3 } from "../scenarios/netrename.mjs";

test("the message is found among the HUD's, its lines joined and colours dropped", () => {
  assert.equal(renameMessage([{ text: "hello" }, { text: "\x1c[Red]The name \"zxalpha\" is already\nin use." }]), "The name \"zxalpha\" is already in use.");
  assert.equal(renameMessage([{ text: "hello" }]), null);
  assert.equal(renameMessage(undefined), null);
});

test("the message passes in double quotes with both sentences ended, and names what it read otherwise", () => {
  assert.deepEqual(judgeMessage("The name \"zxalpha\" is already in use. You are renamed to \"Player 1\"."), []);
  assert.deepEqual(judgeMessage("The name 'zxalpha' is already in use. You are renamed to 'Player 1'."), ["the message reads \"The name 'zxalpha' is already in use. You are renamed to 'Player 1'.\""]);
  assert.deepEqual(judgeMessage(null), ["the renamed player was never told"]);
});

test("the pk3 holds the map", () => {
  assert.ok(buildNetRenamePk3().toString("latin1").includes("maps/MAP01.wad"));
});
