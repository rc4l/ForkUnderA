// [Doruk-Aksoy] ACS line traces, tracer rules and trace callbacks on a netgame (features/acs-traces): exit 0 when acstraces' steps, run as CLIENTSIDE copies on
// the player's client, an observer and a late joiner and then on the server, stop on each machine's own walls, lines, planes and actors where single player
// does, rules and callbacks (tracecb.bcs' copies) deciding the same hits, with each machine's own tracer ids.
//
// usage:  node scenarios/netacstraces.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { withNetCopies, addPlayerStarts, netJudge, onSnapshots } from "../src/netacs.mjs";
import { TRACE_STEPS, WANT, WANT_STRINGS, buildAcsTracesPk3, judgeTraces } from "./acstraces.mjs";

// [Doruk-Aksoy] Every step before acstraces' savegame and the first callback script (30, its copy 1030 in tracecb.o) on every machine, and again on the late
// joiner, a step that logs nothing waiting under a tag of its own; the callback that removes an imp (32) runs last, on every machine with the late joiner, so
// the traces before it all see three imps.
export const STEPS = [...TRACE_STEPS.map(([script, tags]) => ({ script, tags: tags.length ? tags : [`held${script}`] })), { script: 30, tags: ["cb1"], tics: 35 }];
export const LATE = STEPS.map((s) => ({ ...s, server: false }));
export const AFTER = [{ script: 32, tags: ["cb3"], tics: 35 }];
// [Doruk-Aksoy] The callback scripts are tracecb.o's, their copies compiled in beside them.
export const LIBRARY_SCRIPTS = [30, 32];
// [Doruk-Aksoy] North-west, behind the imps (which face east) and off every lane a trace takes.
export const STARTS = [[40, 1000], [140, 1000], [240, 1000]];

export function buildNetTracesPk3() {
  return buildAcsTracesPk3({
    withScripts: (s) => withNetCopies(s, STEPS.map((x) => x.script).filter((n) => !LIBRARY_SCRIPTS.includes(n))),
    withMap: (m) => addPlayerStarts(m, STARTS),
  });
}

// [Doruk-Aksoy] The tags a machine logs online: every one before the savegame, and the two callback steps.
const ONLINE = new Set([...STEPS, ...AFTER].flatMap((s) => s.tags));
const pick = (all) => Object.fromEntries(Object.entries(all).filter(([tag]) => ONLINE.has(tag)));
export const WANT_ONLINE = pick(WANT), STRINGS_ONLINE = pick(WANT_STRINGS);

export const judgeMachine = (logs) => judgeTraces(logs, WANT_ONLINE, STRINGS_ONLINE);

export const judgeNetplay = netJudge({ steps: STEPS, late: LATE, after: AFTER, judge: judgeMachine,
  passed: `${Object.values(WANT_ONLINE).reduce((n, v) => n + v.length, 0) + Object.values(STRINGS_ONLINE).reduce((n, v) => n + v.length, 0)} trace values right on every machine's own walls, lines, planes and actors, rules and callbacks deciding the same hits` });

const MONSTERS = { sv_nomonsters: 0 };
export const CONFIGS = onSnapshots([{ name: "lan", players: 1, observers: 1, serverCvars: MONSTERS }, { name: "ping100", players: 1, observers: 1, lag: "100,100", serverCvars: MONSTERS }]);

runScenarioIfMain(import.meta.url, () => ({ name: "netacstraces", wad: buildNetTracesPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
