import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeJoins, buildNetPlayerJoinsPk3, buildScript, GAMEEVENT_PLAYERJOINS } from "../scenarios/netplayerjoins.mjs";
import { PCD } from "../src/acs.mjs";

const ok = { player: 0, askedAtJoin: 1, namedAtJoin: 0, askedRefused: 2, playingRefused: false, playingAllowed: true };

test("an asked, refusing then allowing event passes", () => {
  assert.deepEqual(judgeJoins(ok), []);
});

test("each wrong step names itself", () => {
  assert.deepEqual(judgeJoins({ ...ok, askedAtJoin: 0, askedRefused: 0 }), ["the event was asked 0 times as the client joined", "the event was not asked again on the refused join"]);
  assert.deepEqual(judgeJoins({ ...ok, namedAtJoin: 3 }), ["the event named player 3, not 0"]);
  assert.deepEqual(judgeJoins({ ...ok, playingRefused: true }), ["the client joined with the event's result at zero"]);
  assert.deepEqual(judgeJoins({ ...ok, playingAllowed: false }), ["the client never joined once the event allowed it"]);
});

test("the EVENT script checks its type and sets a zero result", () => {
  const code = buildScript();
  assert.deepEqual(code.slice(0, 5), [PCD.PUSHSCRIPTVAR, 0, PCD.PUSHNUMBER, GAMEEVENT_PLAYERJOINS, PCD.EQ]);
  assert.ok(code.some((w, i) => w === PCD.SETRESULTVALUE && code[i - 1] === 0 && code[i - 2] === PCD.PUSHNUMBER));
  assert.ok(buildNetPlayerJoinsPk3().length > 0);
});
