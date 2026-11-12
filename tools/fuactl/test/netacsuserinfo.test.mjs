import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScripts, clientScript, startBurstScript, judgeValues, STRINGS, VALUE, BASE, BURST } from "../scenarios/netacsuserinfo.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
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

test("the burst is clientside and closed, loops back past its counter's reset, and the server starts it", () => {
  const [client, server, burst, start] = buildScripts();
  assert.deepEqual([burst.number, burst.type, burst.flags], [3, SCRIPT.CLOSED, SCRIPTF.CLIENTSIDE]);
  assert.deepEqual([start.number, start.type, start.flags], [4, SCRIPT.CLOSED, undefined]);
  const target = burst.code[burst.code.lastIndexOf(PCD.IFGOTO) + 1];
  assert.equal(target, 8 + 4 * (client.code.length + server.code.length + 4));
  assert.ok(burst.code.includes(ACSF.ConsolePlayerNumber) && burst.code.includes(BURST) && burst.code.includes(BASE));
  assert.deepEqual(startBurstScript(), [PCD.PUSHNUMBER, 3, PCD.PUSHNUMBER, 0, PCD.LSPEC1 + 1, SPECIAL.ACS_ExecuteAlways, PCD.TERMINATE]);
});

test("the values pass only when both sides hold them and the player stayed", () => {
  assert.deepEqual(judgeValues({ own: 160, seen: 160, kept: true }, 160), []);
  assert.deepEqual(judgeValues({ own: 160, seen: 143, kept: true }, 160), ["the server holds the player's zx_acsuser as 143, not 160"]);
  assert.deepEqual(judgeValues({ own: NaN, seen: 108, kept: false }, 160), ["the server kicked the player"]);
});
