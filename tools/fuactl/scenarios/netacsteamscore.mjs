// [Doruk-Aksoy] SetTeamScore in a netgame: the server's script sets the blue team's score to 7 and the red's to 3, then every machine's GetTeamScore,
// a CLIENTSIDE copy on the player's client, an observer and a late joiner, reads back 7 and 3, in team deathmatch and in CTF at 100 ms.
// Fails with SVC_SETTEAMSCORE left unsent (fua_debug_netdrop).
//
// usage:  node scenarios/netacsteamscore.mjs
import { SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, netJudge, onSnapshots } from "../src/netacs.mjs";
import { SET_SCRIPT, READ_SCRIPT, buildMap, buildTeamScoreWad, judgeScores } from "./acsteamscore.mjs";

// [Doruk-Aksoy] The server sets the scores alone; every machine then reads them, the late joiner too.
export const STEPS = [{ script: 1, tags: ["ts"], clients: false }, { script: 2, tags: ["tg"] }];
export const LATE = [STEPS[1]];

export function buildNetTeamScoreWad() {
  return buildTeamScoreWad(withNetCopies([{ code: SET_SCRIPT, type: SCRIPT.CLOSED, number: 1 }, { code: READ_SCRIPT, type: SCRIPT.CLOSED, number: 2 }], [2]), buildMap());
}

// [Doruk-Aksoy] The server's set and read, and each client's read of the scores the server sent.
export function judgeMachine(logs, member) {
  return member.role === "server" ? [...judgeScores(logs.ts, "ts"), ...judgeScores(logs.tg, "tg")] : judgeScores(logs.tg, "tg");
}

export const judgeNetplay = netJudge({ steps: STEPS, late: LATE, judge: judgeMachine, by: "server",
  passed: "the server's SetTeamScore set both teams, and every machine's GetTeamScore, a late joiner's too, reads back 7 and 3" });

export const CONFIGS = onSnapshots([
  { name: "teamdm", players: 1, observers: 1, serverCvars: { deathmatch: 1, teamplay: 1, fraglimit: 0 } },
  { name: "ctf", players: 1, observers: 1, lag: "100,100", serverCvars: { ctf: 1, pointlimit: 0 } },
]);

runScenarioIfMain(import.meta.url, () => ({ name: "netacsteamscore", wad: buildNetTeamScoreWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
