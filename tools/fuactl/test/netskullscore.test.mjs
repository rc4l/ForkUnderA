import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeEcho, buildNetSkullScorePk3, CONFIGS, CVAR } from "../scenarios/netskullscore.mjs";

test("a client's echo passes at the server's value and names any other", () => {
  assert.deepEqual(judgeEcho(`"${CVAR}" is "0"`, 0, "joined,"), []);
  assert.deepEqual(judgeEcho(`"${CVAR}" is "false"`, 0, "joined,"), []);
  assert.deepEqual(judgeEcho(`"${CVAR}" is "true"`, 1, "changed,"), []);
  assert.deepEqual(judgeEcho(`"${CVAR}" is "true"`, 0, "joined,"), [`joined, the client's ${CVAR} is true, not the server's 0`]);
  assert.deepEqual(judgeEcho(`Unknown command "${CVAR}"`, 1, "changed,"), [`changed, the client's ${CVAR} is unknown (Unknown command "${CVAR}"), not the server's 1`]);
});

test("the server starts away from the default and the map has a start per player", () => {
  assert.equal(CONFIGS[0].serverCvars[CVAR], 0);
  assert.ok(buildNetSkullScorePk3().toString("latin1").includes("maps/MAP01.wad"));
});
