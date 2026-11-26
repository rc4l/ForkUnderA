// [Doruk-Aksoy] ACS field access on a netgame (features/acs-fields): exit 0 when acsfields' reads and writes, run as CLIENTSIDE copies on the player's client, an
// observer and a late joiner and then on the server, give each machine its own actors' fields, a client's user_ writes staying on that client.
//
// usage:  node scenarios/netacsfields.mjs
import { PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, addPlayerStarts, netJudge } from "../src/netacs.mjs";
import { CHECKS, NEAR, S, fn, buildAcsFieldsPk3, judgeFields } from "./acsfields.mjs";
import { logged } from "./acshandles.mjs";

// [Doruk-Aksoy] The clients run script 1 first, then the server reads the user_ variables they wrote on their own copies (script 2), then runs script 1 itself.
export const STEPS = [{ script: 1, tags: ["f1"], server: false }, { script: 2, tags: ["f2"], clients: false }, { script: 1, tags: ["f1"], clients: false }];
export const LATE = [{ script: 1, tags: ["f1"], server: false }];
export const STARTS = [[64, 64], [448, 64], [448, 448]];

// [Doruk-Aksoy] Script 2: the server's user_count and user_list[1] on the near barrel, before it writes them.
export function serverReadScript() {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S.f2, PCD.PRINTSTRING,
    ...[fn("GetUserVariable", NEAR, S.user_count), fn("GetField", fn("GetActorHandle", NEAR), S.user_list, 1)].flatMap((w) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...w, PCD.PRINTNUMBER]),
    PCD.ENDLOG, PCD.TERMINATE];
}

export function buildNetFieldsPk3() {
  return buildAcsFieldsPk3({
    withScripts: (s) => withNetCopies([...s, { code: serverReadScript(), type: SCRIPT.CLOSED, number: 2 }], [1, 2]),
    withMap: (m) => addPlayerStarts(m, STARTS),
  });
}

// [Doruk-Aksoy] What is wrong with one machine's output: acsfields' checks on its own actors, and on the server no client's write.
export function judgeMachine(logs, member) {
  const faults = judgeFields(logs.f1);
  if (member.role === "server") {
    const got = logged(logs.f2, "f2");
    if (!got || got[0] !== 0 || got[1] !== 0) faults.push(`after the clients wrote theirs, the server's user_count and user_list[1] read ${got ? got.join(" ") : "nothing"}, not 0 0`);
  }
  return faults;
}

export const judgeNetplay = netJudge({ steps: STEPS, late: LATE, judge: judgeMachine,
  passed: `${CHECKS.length} reads and writes by field name give every machine its own actors' fields, a client's user_ writes staying on it` });

export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }, { name: "ping100", players: 1, observers: 1, lag: "100,100" }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsfields", wad: buildNetFieldsPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
