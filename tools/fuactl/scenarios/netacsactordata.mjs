// [Doruk-Aksoy] ACS actor data on a netgame (features/acs-handles): exit 0 when acsactordata's first step, run as CLIENTSIDE copies on the player's client and an
// observer and then on the server, gives each machine a store of its own, a value only the server sets never reaches a client or a late joiner, and a barrel the
// server removes takes each client's values on it along.
//
// usage:  node scenarios/netacsactordata.mjs
import { PCD, SCRIPT } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, addPlayerStarts, netJudge } from "../src/netacs.mjs";
import { KEPT, FULL, S, fn, global, logScript, buildAcsActorDataPk3, judgeFirst, stepValues } from "./acsactordata.mjs";

// [Doruk-Aksoy] The value only the server's script 4 sets on the kept barrel, and the default a machine without it reads.
export const SERVER_VALUE = 77, DEFAULT = -7;

// [Doruk-Aksoy] 1 on every machine, 4 on the server alone (its value, then the full barrel removed), 5 on the clients alone: the server's value, and the removed
// barrel's values through each client's kept handle.
export const STEPS = [{ script: 1, tags: ["d1"] }, { script: 4, tags: ["d4"], clients: false }, { script: 5, tags: ["d5"], server: false }];
export const LATE = [{ script: 5, tags: ["d5"], server: false }];
const OWN = {
  4: logScript("d4", [fn("SetActorData", fn("GetActorHandle", KEPT), S.srv, SERVER_VALUE)], [PCD.PUSHNUMBER, FULL, PCD.LSPEC1, SPECIAL.Thing_Remove]),
  5: logScript("d5", [fn("GetActorData", fn("GetActorHandle", KEPT), S.srv, DEFAULT), fn("GetActorData", global(1), S.k0, -1), fn("HasActorData", global(1), S.k0)]),
};
export const STARTS = [[64, 64], [448, 64], [448, 448]];

export function buildNetActorDataPk3() {
  return buildAcsActorDataPk3({
    withScripts: (s) => withNetCopies([...s, ...Object.entries(OWN).map(([n, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(n) }))], [1, 4, 5]),
    withMap: (m) => addPlayerStarts(m, STARTS),
  });
}

// [Doruk-Aksoy] What is wrong with one machine's output.
export function judgeMachine(logs, member) {
  const faults = member.role === "latejoin" ? [] : judgeFirst(logs);
  if (member.role === "server") {
    const [set] = stepValues(logs, "d4", faults);
    if (set !== 1) faults.push(`its own value took ${set}, not 1`);
    return faults;
  }
  const [server, removed, has] = stepValues(logs, "d5", faults);
  if (server !== DEFAULT) faults.push(`it read the server's value as ${server}, not its own default ${DEFAULT}`);
  if (removed !== -1 || has !== 0) faults.push(`its values on the barrel the server removed read ${removed}, has ${has}, not -1 and 0`);
  return faults;
}

export const judgeNetplay = netJudge({ steps: STEPS, late: LATE, judge: judgeMachine,
  passed: "every machine's values read back from its own store, none of the server's reaching a client or a late joiner, and a barrel the server removed took each client's values along" });

export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }, { name: "ping100", players: 1, observers: 1, lag: "100,100" }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsactordata", wad: buildNetActorDataPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
