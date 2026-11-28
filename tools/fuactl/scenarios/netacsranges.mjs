// [Doruk-Aksoy] Colourise and tint ranges in a netgame: the server's script builds the ACS translations, and exit 0 when every machine, the player's client,
// an observer and a late joiner (which gets them from the join's replay), holds tables equal to the server's, each entry UZDoom's formula, a refused range
// sent to no one and a translation started again with no range the identity everywhere.
// Fails with SVC_CREATETRANSLATION left unsent (fua_debug_netdrop).
//
// usage:  node scenarios/netacsranges.mjs
import { PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, netJudge, who, result, NET_CONFIGS } from "../src/netacs.mjs";
import { buildRangesPk3, judgeTables, acsTranslation, COLOURISE, TINT, IDENTITIES, STRINGS } from "./acsranges.mjs";

// [Doruk-Aksoy] Script 2 only logs, so each client and the late joiner say they ran something after the server built the tables.
export const SEEN_SCRIPT = [PCD.BEGINPRINT, PCD.PUSHNUMBER, STRINGS.indexOf("ranges"), PCD.PRINTSTRING, PCD.ENDLOG, PCD.TERMINATE];
export const STEPS = [{ script: 1, tags: ["ranges"], clients: false }, { script: 2, tags: ["seen"], server: false }];
export const LATE = [STEPS[1]];

export function buildNetRangesPk3() {
  return buildRangesPk3({ withScripts: (s) => withNetCopies([...s, { code: SEEN_SCRIPT, type: SCRIPT.CLOSED, number: 2 }], [2]) });
}

// [Doruk-Aksoy] Each machine logged its step: the server building the tables, every client its read.
export function judgeMachine(logs, member) {
  const tag = member.role === "server" ? "ranges" : "seen";
  return /ranges/.test(String(logs[tag])) ? [] : [`it logged nothing for ${tag}`];
}

const judgeLogs = netJudge({ steps: STEPS, late: LATE, judge: judgeMachine, by: "server", passed: "" });

// [Doruk-Aksoy] The tables every machine holds: each against UZDoom's formula, and each ACS one equal to the server's, remap included.
export async function judgeNetplay(set, label, opts, deps = {}) {
  const { judge = judgeLogs } = deps;
  const logged = await judge(set, label);
  const faults = logged.failed ? [logged.line.replace(/^FAIL [^:]*: /, "")] : [];
  const reader = (m) => (args) => m.c.rpc("world.translation", args).catch(() => null);
  const server = {};
  for (const n of [COLOURISE.acs, TINT.acs, ...IDENTITIES]) server[n] = await reader(set.server)({ translation: acsTranslation(n) });
  for (const m of [set.server, ...set.clients]) {
    faults.push(...(await judgeTables(reader(m))).map((f) => `${who(m)}: ${f}`));
    if (m === set.server) continue;
    for (const n of [COLOURISE.acs, TINT.acs, ...IDENTITIES]) {
      const t = await reader(m)({ translation: acsTranslation(n) });
      if (t && server[n] && (t.palette.join() !== server[n].palette.join() || t.remap.join() !== server[n].remap.join()))
        faults.push(`${who(m)}: ACS translation ${n} differs from the server's`);
    }
  }
  return result(label, faults, "every machine, a late joiner too, holds the server's colourise and tint tables, each entry UZDoom's formula, and the identity where it refused ranges or started a translation again");
}

export const CONFIGS = NET_CONFIGS;

runScenarioIfMain(import.meta.url, () => ({ name: "netacsranges", wad: buildNetRangesPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
