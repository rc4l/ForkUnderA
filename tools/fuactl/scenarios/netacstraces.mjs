// [Doruk-Aksoy] ACS line traces on a netgame (features/acs-traces): exit 0 when acstraces' steps, run as CLIENTSIDE copies on the player's client, an observer
// and a late joiner and then on the server, stop on each machine's own walls, lines, planes and actors where single player does, with each machine's own
// tracer ids.
//
// usage:  node scenarios/netacstraces.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, addPlayerStarts, netJudge, onSnapshots } from "../src/netacs.mjs";
import { TRACE_STEPS, WANT, WANT_STRINGS, buildAcsTracesPk3, judgeTraces } from "./acstraces.mjs";

// [Doruk-Aksoy] Every step before acstraces' savegame on every machine, and again on the late joiner; a step that logs nothing waits under a tag of its own.
export const STEPS = TRACE_STEPS.map(([script, tags]) => ({ script, tags: tags.length ? tags : [`held${script}`] }));
export const LATE = STEPS.map((s) => ({ ...s, server: false }));
export const AFTER = [];
// [Doruk-Aksoy] North-west, behind the imps (which face east) and off every lane a trace takes.
export const STARTS = [[40, 1000], [140, 1000], [240, 1000]];

export function buildNetTracesPk3() {
  return buildAcsTracesPk3({ withScripts: (s) => withNetCopies(s, STEPS.map((x) => x.script)), withMap: (m) => addPlayerStarts(m, STARTS) });
}

// [Doruk-Aksoy] The tags a machine logs online, every one before the savegame.
const ONLINE = new Set([...STEPS, ...AFTER].flatMap((s) => s.tags));
const pick = (all) => Object.fromEntries(Object.entries(all).filter(([tag]) => ONLINE.has(tag)));
export const WANT_ONLINE = pick(WANT), STRINGS_ONLINE = pick(WANT_STRINGS);

export const judgeMachine = (logs) => judgeTraces(logs, WANT_ONLINE, STRINGS_ONLINE);

export const judgeNetplay = netJudge({ steps: STEPS, late: LATE, after: AFTER, judge: judgeMachine,
  passed: `${Object.values(WANT_ONLINE).reduce((n, v) => n + v.length, 0) + Object.values(STRINGS_ONLINE).reduce((n, v) => n + v.length, 0)} trace values right on every machine's own walls, lines, planes and actors` });

const MONSTERS = { sv_nomonsters: 0 };
export const CONFIGS = onSnapshots([{ name: "lan", players: 1, observers: 1, serverCvars: MONSTERS }, { name: "ping100", players: 1, observers: 1, lag: "100,100", serverCvars: MONSTERS }]);

runScenarioIfMain(import.meta.url, () => ({ name: "netacstraces", wad: buildNetTracesPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
