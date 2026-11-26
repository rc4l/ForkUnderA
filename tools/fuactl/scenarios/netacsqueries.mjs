// [Doruk-Aksoy] ACS actor queries on a netgame (features/acs-queries): exit 0 when acsqueries' blockmap and class queries, the 16-a-script cap and a held query
// another script tries, run as CLIENTSIDE copies on the player's client, an observer and a late joiner and then on the server, find in each machine's own world
// what each filter keeps, with each machine's own query ids.
//
// usage:  node scenarios/netacsqueries.mjs
import { PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, addPlayerStarts, netJudge, answerFor } from "../src/netacs.mjs";
import { G, FQ, fn, global, keep, run, find, filter, logScript, TID, ROOM1, WANT, buildAcsQueriesPk3, judgeQueries } from "./acsqueries.mjs";

// [Doruk-Aksoy] 9 kills the imp on the server alone, as a client's damage stays its own; 10 keeps each client's handles for the filters; q1 and q2 then run
// everywhere, again on the late joiner; the cap, the held query and the class queries (which remove barrel A, so last) run on every machine after it joined.
export const STEPS = [{ script: 9, tags: ["kill"], clients: false, tics: 35 }, { script: 10, tags: ["q0"], server: false }, { script: 1, tags: ["q1"] }, { script: 11, tags: ["q2"] }];
export const LATE = [{ script: 10, tags: ["q0"], server: false }, { script: 1, tags: ["q1"], server: false }, { script: 11, tags: ["q2"], server: false }];
export const AFTER = [{ script: 3, tags: ["l1"] }, { script: 4, tags: ["h1"] }, { script: 5, tags: ["x1"] }, { script: 2, tags: ["c1", "c2"], tics: 40 }];
const PQ = 10;
// [Doruk-Aksoy] South of every monster, which faces north, so none sees a player and wakes.
export const STARTS = [[100, 100], [950, 100], [800, 60]];
export const HANDLES_SCRIPT = logScript("q0", [global(G.PLAYER)],
  [...keep(G.PLAYER, fn("GetActorHandle", 0)), ...keep(G.E, fn("GetActorHandle", TID.E)), ...keep(G.FRIEND1, fn("GetActorHandle", TID.FRIEND1))]);

// [Doruk-Aksoy] Script 11: FQ_PLAYER finds every player, and its activator among the first three exactly once, whatever order the blockmap gives them.
export const PLAYERS_SCRIPT = logScript("q2", [
  [...keep(PQ, fn("QueryCreate")), ...run(filter(global(PQ), FQ.PLAYER)), ...find(global(PQ), ...ROOM1)],
  [PCD.PUSHNUMBER, 0, ...[0, 1, 2].flatMap((i) => [...fn("FoundActor", global(PQ), i), ...global(G.PLAYER), PCD.EQ, PCD.ADD])],
]);

export function buildNetQueriesPk3() {
  return buildAcsQueriesPk3({
    withScripts: (s) => withNetCopies([...s, { code: HANDLES_SCRIPT, type: SCRIPT.CLOSED, number: 10 }, { code: PLAYERS_SCRIPT, type: SCRIPT.CLOSED, number: 11 }],
      [1, 2, 3, 4, 5, 9, 10, 11]),
    withMap: (m) => addPlayerStarts(m, STARTS),
  });
}

// [Doruk-Aksoy] FQ_PLAYER counts every player in the game, the player and the observer, then the late joiner too; q1's check that the first found is the
// activator holds only alone, so q2 checks it among them instead.
const PLAYERS_AT = 13, FIRST_IS_OURS = 14;
export const ANSWERS = { client: { players: 2 }, latejoin: { players: 3 } };

// [Doruk-Aksoy] One machine's tags and answers: WANT's for the tags it ran, q1 with its own player count, and q2.
export function wantFor(role) {
  const tags = ["q1", "c1", "c2", "l1", "h1", "x1"];
  const want = Object.fromEntries(tags.map((t) => [t, [...WANT[t]]]));
  const players = answerFor(ANSWERS, role, "players", 2);
  want.q1[PLAYERS_AT] = players;
  want.q1.splice(FIRST_IS_OURS, 1, undefined);
  want.q2 = [players, 1];
  return want;
}

export const judgeMachine = (logs, member) => judgeQueries(logs, wantFor(member.role));

export const judgeNetplay = netJudge({ steps: STEPS, late: LATE, after: AFTER, judge: judgeMachine,
  passed: "every machine's blockmap and class queries find in its own world what each filter keeps, with its own ids, cap and held queries" });

const MONSTERS = { sv_nomonsters: 0 };
export const CONFIGS = [{ name: "lan", players: 1, observers: 1, serverCvars: MONSTERS }, { name: "ping100", players: 1, observers: 1, lag: "100,100", serverCvars: MONSTERS }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsqueries", wad: buildNetQueriesPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
