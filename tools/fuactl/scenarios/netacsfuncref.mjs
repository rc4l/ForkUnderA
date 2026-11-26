// [Doruk-Aksoy] ACS function references on a netgame (features/acs-funcrefs): exit 0 when acsfuncref's steps, each run on the server and as a CLIENTSIDE copy
// on the player's client and an observer, give every machine the same answers across two changemaps: references compare equal between modules, survive the map
// changes in each machine's own global variables, and one into a library MAP03 does not load stops its script.
//
// usage:  node scenarios/netacsfuncref.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { addPlayerStarts, runSteps, changeMap, judgeEach, result } from "../src/netacs.mjs";
import { buildAcsFuncRefPk3, judgeSteps } from "./acsfuncref.mjs";

// [Doruk-Aksoy] Each map's steps: the scripts fixtures/acsfuncref runs there, each with its copy at 190N.
export const MAPS = [
  { map: "MAP01", steps: [{ script: 901, tags: ["map01"] }] },
  { map: "MAP02", steps: [{ script: 904, tags: ["map02map"] }, { script: 902, tags: ["map02"] }] },
  { map: "MAP03", steps: [{ script: 903, tags: ["map03"] }] },
];

export const STARTS = [[64, 192], [128, 192], [192, 192]];

export function buildNetFuncRefPk3() {
  return buildAcsFuncRefPk3({ withMap: (m) => addPlayerStarts(m, STARTS) });
}

// [Doruk-Aksoy] Each machine's output on every map, merged by tag.
export function mergeLogs(rounds) {
  const out = new Map();
  for (const logs of rounds) for (const [m, l] of logs) out.set(m, { ...(out.get(m) || {}), ...l });
  return out;
}

export async function judgeNetplay(set, label) {
  await cap.waitTics(set.server.c, 35);
  const rounds = [], faults = [];
  for (const [i, { map, steps }] of MAPS.entries()) {
    if (i) faults.push(...(await changeMap(set, map)));
    // [Doruk-Aksoy] The server's console runs both, since no step reads its activator.
    rounds.push(await runSteps(set, steps, { by: set.server }));
  }
  faults.push(...judgeEach(mergeLogs(rounds), (logs) => judgeSteps(logs)));
  return result(label, faults, "every machine's references compare equal between modules, survive two changemaps in its own global variables, and one into an unloaded library stops its script");
}

export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }, { name: "ping100", players: 1, observers: 1, lag: "100,100" }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsfuncref", wad: buildNetFuncRefPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
