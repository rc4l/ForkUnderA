// [Doruk-Aksoy] UZDoom's ACS functions on a netgame (features/acs-parity): exit 0 when acsparity's checks, run by the player on the server and as CLIENTSIDE
// copies on the player's client, an observer and a late joiner, give each machine its right answer.
//
// usage:  node scenarios/netacsparity.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { withNetCopies, addPlayerStarts, runSteps, judgeEach, result } from "../src/netacs.mjs";
import { CHECKS, buildAcsParityPk3, parseParity, judgeNullDamage } from "./acsparity.mjs";

export const STEPS = [{ script: 1, tags: ["parity"] }, { script: 2, tags: ["damagenull"] }];

// [Doruk-Aksoy] Where players 2 to 4 start, clear of the first player's and the barrels.
export const STARTS = [[48, 48], [48, 160], [464, 464]];

export function buildNetParityPk3() {
  return buildAcsParityPk3({ withScripts: (s) => withNetCopies(s, STEPS.map((x) => x.script)), withMap: (m) => addPlayerStarts(m, STARTS) });
}

// [Doruk-Aksoy] Where a machine's right answer is not single player's: a client's copy leaves a synced actor's health to the server (the barrel keeps 20),
// and the late joiner arrives after the server's run set the player's step height and took the barrel to 10.
const CLIENT = { "DamageActor(barrel, 10, Fire)": -1, "the barrel's health after": 20, "the new activator's health": 20 };
export const ANSWERS = {
  player: CLIENT,
  observer: CLIENT,
  latejoin: { ...CLIENT, "APROP_MaxStepHeight": 40 * 65536, "the barrel's health after": 10, "the new activator's health": 10 },
};

// [Doruk-Aksoy] Each check's answer for `role`: ANSWERS', else acsparity's.
export const wantFor = (check, role) => (ANSWERS[role] && check.what in ANSWERS[role] ? ANSWERS[role][check.what] : check.want);

// [Doruk-Aksoy] What is wrong with one machine's output.
export function judgeMachine(logs, member) {
  const got = parseParity(logs.parity);
  if (!got) return [`script 1 logged no checks (${String(logs.parity || "").trim().slice(0, 120)})`];
  if (got.length !== CHECKS.length) return [`script 1 logged ${got.length} values, not ${CHECKS.length}`];
  const faults = CHECKS.flatMap((c, i) => {
    const want = wantFor(c, member.role);
    return got[i] === want ? [] : [`${c.what} is ${JSON.stringify(got[i])}, not ${JSON.stringify(want)}`];
  });
  return [...faults, ...judgeNullDamage(logs.damagenull)];
}

export async function judgeNetplay(set, label) {
  await cap.waitTics(set.server.c, 35);
  const first = await runSteps(set, STEPS);
  const late = await addClient(set, { role: "latejoin" });
  await cap.waitTics(set.server.c, 35);
  const joined = await runSteps(set, STEPS, { on: [late], server: false });
  const faults = [...judgeEach(first, judgeMachine), ...judgeEach(joined, judgeMachine)];
  return result(label, faults, `${CHECKS.length} checks give each machine its answer: the server as single player, every client from its own world, a client's copy changing nothing on the server`);
}

export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }, { name: "ping100", players: 1, observers: 1, lag: "100,100" }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsparity", wad: buildNetParityPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
