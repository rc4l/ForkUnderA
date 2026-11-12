import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScripts, clientScript, STRINGS, VALUE } from "../scenarios/netacsuserinfo.mjs";
import { ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";

test("the client's ENTER script is clientside and sets its own player's user cvar", () => {
  const [client, server] = buildScripts();
  assert.equal(client.type, SCRIPT.ENTER);
  assert.equal(client.flags, SCRIPTF.CLIENTSIDE);
  assert.equal(server.flags, undefined);
  assert.deepEqual(clientScript().slice(0, 8), [PCD.PLAYERNUMBER, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, VALUE, PCD.CALLFUNC, 3, ACSF.SetUserCVar]);
});

test("the server's loop jumps back to its own first word, laid after the client's", () => {
  const [client, server] = buildScripts();
  const target = server.code[server.code.lastIndexOf(PCD.GOTO) + 1];
  assert.equal(target, 8 + 4 * client.code.length);
  assert.ok(server.code.includes(ACSF.GetUserCVar));
  assert.equal(STRINGS.length, 2);
});
