import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeLog, buildScript, buildSkullAssistPk3, SIDES, TPROP, TEAM, TEAM_STARTS, TEAMINFO } from "../scenarios/skullassist.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("the log passes with the console player carrying and the wanted assister", () => {
  assert.deepEqual(judgeLog("carrier 0 assister -1", -1), []);
  assert.deepEqual(judgeLog("carrier 0 assister 0", -1), ["Blue's assist goes to 0, not -1"]);
  assert.deepEqual(judgeLog("carrier -1 assister 0", 0), ["Red's skull is carried by -1, not the console player"]);
  assert.deepEqual(judgeLog("nothing", 0), ["script 1 logged no carrier and assister (nothing)"]);
});

test("the script reads Red's carrier then Blue's assister", () => {
  const code = buildScript();
  assert.deepEqual(code.slice(4, 10), [PCD.PUSHNUMBER, TEAM.Red, PCD.PUSHNUMBER, TPROP.Carrier, PCD.CALLFUNC, 2]);
  assert.equal(code.filter((op) => op === ACSF.GetTeamProperty).length, 2);
});

test("the cvar on wants player 0, off or turned off after the return wants nobody, and the pk3 holds both team starts", () => {
  assert.deepEqual(SIDES.map((s) => [s.launchCvars.sv_requireskulltoscore, !!s.turnOff, s.want]), [[1, false, 0], [0, false, -1], [1, true, -1]]);
  assert.ok(TEAMINFO.includes(`PlayerStartThingNumber ${TEAM_STARTS.Blue}`) && TEAMINFO.includes(`PlayerStartThingNumber ${TEAM_STARTS.Red}`));
  const bytes = buildSkullAssistPk3().toString("latin1");
  for (const n of ["botinfo.txt", "teaminfo.txt", `type = ${TEAM_STARTS.Blue};`, `type = ${TEAM_STARTS.Red};`]) assert.ok(bytes.includes(n), n);
});
