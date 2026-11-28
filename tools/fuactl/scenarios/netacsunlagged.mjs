// [Doruk-Aksoy] ACS traces under unlagged: the first player's client runs a NET script whose three traces aim at where the second player stands
// on the server now. Rewound to what the shooter saw, the trace misses a target strafing across it at 200 ms; TRF_FUA_NOUNLAGGED and a
// MapSpot's trace, no player's, hit it; with the target still, all three hit. Fails on a build whose traces don't rewind, and with sv_nounlagged 1.
//
// usage:  node scenarios/netacsunlagged.mjs
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { onSnapshots, result } from "../src/netacs.mjs";
import * as cap from "../src/capture.mjs";

// [Doruk-Aksoy] The probe library, compiled by zt-bcc from unlagged.bcs beside it.
export const PROBE = fileURLToPath(new URL("../fixtures/acsunlagged/unlagged.o", import.meta.url));
// [Doruk-Aksoy] The MapSpot's tid, the probe's SPOT_TID.
export const SPOT_TID = 21;
// [Doruk-Aksoy] Probes a config takes, each in the middle of a strafe when the target moves.
export const SAMPLES = 4;

// [Doruk-Aksoy] A long room: the shooter's start facing east at the target's, which faces back, and the MapSpot south of the shooter with a clear line to the target.
export function buildUnlaggedPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 192, y: 512, angle: 0, type: 1 });
  m.thing({ x: 576, y: 512, angle: 180, type: 2 });
  m.thing({ x: 192, y: 384, angle: 0, type: THING.MapSpot, id: SPOT_TID });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "acs/unlagged.o", data: fs.readFileSync(PROBE) }, { name: "LOADACS.txt", data: "unlagged\n" }]);
}

// [Doruk-Aksoy] The probe's three answers from a log, as [rewound, now, monster], or null.
export function parseProbe(text) {
  const m = /unl (\d) (\d) (\d)/.exec(String(text));
  return m ? [Number(m[1]), Number(m[2]), Number(m[3])] : null;
}

// [Doruk-Aksoy] What is wrong with a config's samples: every opted-out and MapSpot trace hits, and the rewound one hits a still target and misses a strafing one.
export function judgeSamples(samples, strafing) {
  const faults = [];
  samples.forEach((s, i) => {
    if (!s) return faults.push(`sample ${i} logged nothing`);
    const [rewound, now, monster] = s;
    if (now !== 1) faults.push(`sample ${i}: TRF_FUA_NOUNLAGGED missed the target where it stands`);
    if (monster !== 1) faults.push(`sample ${i}: the MapSpot's trace missed the target where it stands`);
    if (strafing && rewound !== 0) faults.push(`sample ${i}: the rewound trace hit the target where it stands, not where the shooter saw it`);
    if (!strafing && rewound !== 1) faults.push(`sample ${i}: the rewound trace missed a target that stood still`);
  });
  return faults;
}

// [Doruk-Aksoy] One probe from the shooter's client, the server's log line read back, waiting on until it comes.
async function probe(set, shooter, deps = {}) {
  const { exec = cap.exec, listen = cap.listen, waitTics = cap.waitTics } = deps;
  const heard = listen(set.server.c);
  await exec(shooter.c, "puke 1", { quietMs: 60 });
  for (let t = 0; t < 70 && !parseProbe(heard.text()); t += 5) await waitTics(set.server.c, 5);
  heard.off();
  return parseProbe(heard.text());
}

export async function judgeNetplay(set, label, { config } = {}, deps = {}) {
  const { exec = cap.exec, sleep = cap.sleep, waitTics = cap.waitTics, take = probe } = deps;
  const [shooter, target] = set.clients;
  await waitTics(set.server.c, 35);
  const strafing = !!config?.strafe;
  const samples = [];
  for (let i = 0; i < SAMPLES; i++) {
    const dir = i % 2 ? "moveleft" : "moveright";
    if (strafing) await exec(target.c, `+${dir}`, { quietMs: 40 });
    if (strafing) await sleep(600);
    samples.push(await take(set, shooter, deps));
    if (strafing) await exec(target.c, `-${dir}`, { quietMs: 40 });
  }
  return result(label, judgeSamples(samples, strafing),
    strafing ? `${SAMPLES} rewound traces missed a strafing target where it stands, as the shooter saw it elsewhere; opted-out and MapSpot traces hit it`
      : `${SAMPLES} traces of each kind hit a target standing still`);
}

// [Doruk-Aksoy] Two players on snapshots: a still target on a LAN, a strafing one at 200 ms of ping.
export const CONFIGS = onSnapshots([{ name: "lan", players: 2, observers: 0 }, { name: "ping200", players: 2, observers: 0, lag: "200", strafe: true }]);

runScenarioIfMain(import.meta.url, () => ({ name: "netacsunlagged", wad: buildUnlaggedPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
