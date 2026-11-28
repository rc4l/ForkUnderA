// [Doruk-Aksoy] UZDoom's ACS functions on a netgame (features/acs-parity): exit 0 when acsparity's checks, run by the player on the server and as CLIENTSIDE
// copies on the player's client, an observer and a late joiner, give each machine its right answer, and SetSectorDamage hurts the player on the server alone,
// every client then holding the health the server left.
//
// usage:  node scenarios/netacsparity.mjs
import { PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { withNetCopies, addPlayerStarts, netJudge, answerFor, who, result, NET_CONFIGS } from "../src/netacs.mjs";
import { CHECKS, S, PLAYERHEALTH, BARREL, ICE, buildAcsParityPk3, parseParity, judgeNullDamage, judgeSectorDamage, sectorHealths, judgeTranslations } from "./acsparity.mjs";
import { ACSF, callFunc } from "../src/acs.mjs";

// [Doruk-Aksoy] The checks, then SetSectorDamage's two scripts on every machine (a client's own call hurting no one), then each client reading the player's
// health the server left (script 5); the late joiner runs the checks and that read.
export const STEPS = [{ script: 1, tags: ["parity"] }, { script: 2, tags: ["damagenull"] }, { script: 3, tags: ["sd3"], tics: 200 },
  { script: 4, tags: ["sd4"], tics: 230 }, { script: 5, tags: ["sdh"], server: false }, { script: 6, tags: ["st"], clients: false }];
export const LATE = [STEPS[0], STEPS[1], STEPS[4]];
export const HEALTH_SCRIPT = [PCD.BEGINPRINT, PCD.PUSHNUMBER, S.sdh, PCD.PRINTSTRING, PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, PLAYERHEALTH, PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE];
// [Doruk-Aksoy] Script 6, the server's alone: the net barrel on Ice, which a client can only hold through the server's send.
export const SERVER_TRANSLATION_SCRIPT = [...callFunc(ACSF.SetTranslation, [BARREL.net, S.Ice]), PCD.DROP, PCD.TERMINATE];

// [Doruk-Aksoy] Where players 2 to 4 start, clear of the first player's and the barrels.
export const STARTS = [[48, 48], [48, 160], [464, 464]];

export function buildNetParityPk3() {
  return buildAcsParityPk3({
    withScripts: (s) => withNetCopies([...s, { code: HEALTH_SCRIPT, type: SCRIPT.CLOSED, number: 5 }, { code: SERVER_TRANSLATION_SCRIPT, type: SCRIPT.CLOSED, number: 6 }],
      STEPS.map((x) => x.script)),
    withMap: (m) => addPlayerStarts(m, STARTS),
  });
}

// [Doruk-Aksoy] Where a machine's right answer is not single player's: a client's copy leaves a synced actor's health to the server (the barrel keeps 20),
// and the late joiner arrives after the server's run set the player's step height and took the barrel to 10.
export const ANSWERS = {
  client: { "DamageActor(barrel, 10, Fire)": -1, "the barrel's health after": 20, "the new activator's health": 20 },
  latejoin: { "APROP_MaxStepHeight": 40 * 65536, "the barrel's health after": 10, "the new activator's health": 10 },
};
// [Doruk-Aksoy] A client's SetSectorDamage copies log the health the server holds then: untouched through script 3, the server's 90 through script 4; and after
// both, the server's 70.
export const CLIENT_HEALTHS = { sd3: [100, 100], sd4: [90, 90, 90], after: 70 };

// [Doruk-Aksoy] Each check's answer for `role`: ANSWERS', else acsparity's.
export const wantFor = (check, role) => answerFor(ANSWERS, role, check.what, check.want);

// [Doruk-Aksoy] What is wrong with one machine's output.
export function judgeMachine(logs, member) {
  const got = parseParity(logs.parity);
  if (!got) return [`script 1 logged no checks (${String(logs.parity || "").trim().slice(0, 120)})`];
  if (got.length !== CHECKS.length) return [`script 1 logged ${got.length} values, not ${CHECKS.length}`];
  const faults = CHECKS.flatMap((c, i) => {
    const want = wantFor(c, member.role);
    return got[i] === want ? [] : [`${c.what} is ${JSON.stringify(got[i])}, not ${JSON.stringify(want)}`];
  });
  faults.push(...judgeNullDamage(logs.damagenull));
  if (member.role === "server") return [...faults, ...judgeSectorDamage(logs.sd3, logs.sd4)];
  const health = (tag) => sectorHealths(logs[tag]) || [];
  for (const tag of member.role === "latejoin" ? [] : ["sd3", "sd4"])
    if (health(tag).join() !== CLIENT_HEALTHS[tag].join()) faults.push(`its ${tag} copy logged healths ${health(tag)}, not ${CLIENT_HEALTHS[tag]}`);
  const after = /sdh (-?\d+)/.exec(String(logs.sdh));
  if (!after || Number(after[1]) !== CLIENT_HEALTHS.after) faults.push(`it holds the player's health at ${after ? after[1] : "nothing"}, not the server's ${CLIENT_HEALTHS.after}`);
  return faults;
}

// [Doruk-Aksoy] The player's client turns god mode off, which the netgame's players start with, so a sector can hurt it.
export async function godOff(set, deps = {}) {
  const { exec = cap.exec } = deps;
  for (let i = 0; i < 2 && !/(God|Degreelessness) mode off/i.test(await exec(set.clients[0].c, "god", { quietMs: 200 })); i++);
}

const PASSED = `${CHECKS.length} checks give each machine its answer, a client's copy changing nothing on the server, SetTranslation's barrels end the same on every machine, and SetSectorDamage hurts the player on the server alone, every client holding its health`;
const judgeLogs = netJudge({ steps: STEPS, late: LATE, judge: judgeMachine, before: godOff, passed: PASSED });

// [Doruk-Aksoy] The logs' faults, then each machine's barrels from its own world.actors, the late joiner's too, which SetTranslation left on Ice but the one given back.
export async function judgeNetplay(set, label, opts, deps = {}) {
  const { judge = judgeLogs } = deps;
  const logged = await judge(set, label);
  const faults = logged.failed ? [logged.line.replace(/^FAIL [^:]*: /, "")] : [];
  for (const m of [set.server, ...set.clients])
    faults.push(...judgeTranslations((await m.c.rpc("world.actors", {}).catch(() => null))?.actors, { far: ICE, mud: ICE, wet: 0, net: ICE }).map((f) => `${who(m)}: ${f}`));
  return result(label, faults, PASSED);
}

export const CONFIGS = NET_CONFIGS;

runScenarioIfMain(import.meta.url, () => ({ name: "netacsparity", wad: buildNetParityPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
