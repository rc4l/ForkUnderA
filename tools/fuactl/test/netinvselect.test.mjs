import { test } from "node:test";
import assert from "node:assert/strict";
import { selectedIn, judgeSelections, buildScripts, buildNetInvSelectPk3, STRINGS, FIRST, SECOND, GIVE, READ, SELECT } from "../scenarios/netinvselect.mjs";
import { ACSF, PCD, SCRIPTF } from "../src/acs.mjs";

test("a log line names the selected item, nothing when it is empty, and null when it is missing", () => {
  assert.equal(selectedIn("server ZxCharmB\n", "server"), "ZxCharmB");
  assert.equal(selectedIn("noise\nclient \n", "client"), "");
  assert.equal(selectedIn("server ZxCharmB\n", "client"), null);
});

test("both sides must hold the first item once given and the second once selected", () => {
  const given = { server: FIRST, client: FIRST }, selected = { server: SECOND, client: SECOND };
  assert.deepEqual(judgeSelections({ given, selected }), []);
  assert.deepEqual(judgeSelections({ given, selected: { server: SECOND, client: FIRST } }), ["the client had ZxCharmA selected once the server selected the second, not ZxCharmB"]);
  assert.deepEqual(judgeSelections({ given: { server: "", client: null }, selected }), ["the server had nothing selected once both were given, not ZxCharmA", "the client logged no selection once both were given"]);
});

test("the give and select scripts run on the server, the select one with the bar shown, and the read script on the client", () => {
  const byNumber = Object.fromEntries(buildScripts().map((s) => [s.number, s]));
  assert.equal(byNumber[GIVE].flags, SCRIPTF.NET);
  assert.equal(byNumber[SELECT].flags, SCRIPTF.NET);
  assert.equal(byNumber[READ].flags, SCRIPTF.NET | SCRIPTF.CLIENTSIDE);
  assert.ok(!byNumber[GIVE].code.includes(ACSF.SetSelectedInventory));
  const code = byNumber[SELECT].code, at = code.indexOf(ACSF.SetSelectedInventory);
  assert.deepEqual(code.slice(at - 6, at + 1), [PCD.PUSHNUMBER, STRINGS.indexOf(SECOND), PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.SetSelectedInventory]);
  assert.ok(byNumber[READ].code.includes(ACSF.GetSelectedInventory));
  assert.ok(buildNetInvSelectPk3().toString("latin1").includes("+INVENTORY.INVBAR"));
});
