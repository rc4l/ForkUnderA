// [Doruk-Aksoy] ACS named random streams on a netgame (features/acs-random): exit 0 when acsrandom's seeded stream, run as CLIENTSIDE copies on the
// player's client, an observer and a late joiner, gives every client the same numbers, and the server's own seeded stream repeats its sequence.
//
// usage:  node scenarios/netacsrandom.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { withNetCopies, addPlayerStarts, runSteps, who, result, NET_CONFIGS } from "../src/netacs.mjs";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, SCRIPT } from "../src/acs.mjs";
import { SCRIPTS, STRINGS, logged } from "./acsrandom.mjs";

const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [Doruk-Aksoy] Seeding and the first five, then the next five.
export const STEPS = [{ script: 1, tags: ["r1"] }, { script: 2, tags: ["r2"] }];
export const STARTS = [[64, 192], [128, 192], [192, 192]];

export function buildNetRandomPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 128, y: 64, angle: 90, type: 1 });
  addPlayerStarts(m, STARTS);
  const scripts = withNetCopies([1, 2].map((n) => ({ code: SCRIPTS[n].code, type: SCRIPT.CLOSED, number: n })), [1, 2]);
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS, { enhanced: true }) })) }]);
}

// [Doruk-Aksoy] What is wrong with every machine's numbers: each client's the same as every other client's, and the server's the same on its second run.
export function judgeMachines(first, again) {
  const faults = [];
  const numbers = (logs) => ({ r1: logged(logs.r1, "r1"), r2: logged(logs.r2, "r2") });
  const clients = [...first].filter(([m]) => m.role !== "server").map(([m, l]) => [m, numbers(l)]);
  const [ref, refNumbers] = clients[0] || [];
  for (const [m, n] of clients) {
    for (const tag of ["r1", "r2"]) {
      if (!n[tag]) faults.push(`${who(m)} logged no ${tag}`);
      else if (refNumbers[tag] && n[tag].join() !== refNumbers[tag].join()) faults.push(`${who(m)}'s ${tag} ${n[tag]} isn't ${who(ref)}'s ${refNumbers[tag]}`);
    }
  }
  const server = [...first].find(([m]) => m.role === "server"), second = [...again].find(([m]) => m.role === "server");
  const a = server && numbers(server[1]).r1, b = second && numbers(second[1]).r1;
  if (!a || !b || a.join() !== b.join()) faults.push(`the server's seeded stream gave ${a}, then ${b}`);
  return faults;
}

export async function judgeNetplay(set, label) {
  await cap.waitTics(set.server.c, 35);
  const late = await addClient(set, { role: "latejoin" });
  await cap.waitTics(set.server.c, 35);
  const first = await runSteps(set, STEPS);
  const again = await runSteps(set, [{ ...STEPS[0], clients: false }], { on: [] });
  return result(label, judgeMachines(first, again), "every client's seeded clientside stream gives the same numbers, a late joiner's too, and the server's seeded stream repeats");
}

export const CONFIGS = NET_CONFIGS;

runScenarioIfMain(import.meta.url, () => ({ name: "netacsrandom", wad: buildNetRandomPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
