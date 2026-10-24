// [rc4l] A cooperative game with friendly fire where one player fires a pistol at another strafing across its aim while an observer watches beside it: exit 0 when, shot for shot, the server's unlagged put the target within OFFSET_BAR units of where the shooter's client held it, and the observer holds the target's health as the server does, on loopback and at 100 ms of ping.
//
// usage:  node scenarios/unlaggedhit.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { parseConsoleJson } from "../src/telemetry.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] Map units a shot's target may sit apart between the server's rewind and the shooter's client; set from the measured baseline.
export const OFFSET_BAR = 2;
// [rc4l] Shots each side may count apart and still be matched one by one.
export const COUNT_SLACK = 2;

// [rc4l] A long room: the shooter's start facing east at the target's, which faces back, and the observer's beside the shooter out of the line of fire.
export function buildUnlaggedHitWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 192, y: 512, angle: 0, type: 1 });
  m.thing({ x: 576, y: 512, angle: 180, type: 2 });
  m.thing({ x: 192, y: 256, angle: 0, type: 3 });
  return wad(mapLumps("MAP01", m));
}

const median = (xs) => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[Math.floor(s.length / 2)] : NaN; };
const quantile = (xs, q) => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[Math.min(s.length - 1, Math.floor(q * s.length))] : NaN; };

// [rc4l] Each shot matched by number: the target's distance between the server's rewound place and the shooter's client's, and between its present place and the client's.
export function shotOffsets(serverReport, clientReport, target) {
  const byN = new Map((clientReport?.shots || []).map((s) => [s.n, s]));
  const out = [];
  for (const s of serverReport?.shots || []) {
    const c = byN.get(s.n);
    const a = (s.players || []).find((p) => p.p === target), b = (c?.players || []).find((p) => p.p === target);
    if (!a || !b) continue;
    out.push({ n: s.n, rewound: Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z), present: Math.hypot(a.px - b.x, a.py - b.y, a.pz - b.z), back: s.tic - s.rewind, hit: s.hit === target });
  }
  return out;
}

// [rc4l] The verdict from the matched shots, each side's shot count and whether the observer agrees with the server on the target's health.
export function judgeUnlagged(label, { offsets, serverShots, clientShots, observerHealth, serverHealth }) {
  const faults = [];
  if (offsets.length < 4) faults.push(`only ${offsets.length} shots matched (server ${serverShots}, client ${clientShots})`);
  if (Math.abs(serverShots - clientShots) > COUNT_SLACK) faults.push(`the server counted ${serverShots} shots and the shooter ${clientShots}`);
  const rewound = offsets.map((o) => o.rewound), present = offsets.map((o) => o.present);
  const p50 = median(rewound);
  if (!(p50 <= OFFSET_BAR)) faults.push(`the target sat ${p50.toFixed(1)} units (median) from where the shooter held it, past ${OFFSET_BAR}`);
  if (observerHealth !== serverHealth) faults.push(`the observer holds the target at ${observerHealth} health, the server at ${serverHealth}`);
  const said = `${offsets.length} shots, ${offsets.filter((o) => o.hit).length} hits, rewound offset p50 ${p50.toFixed(2)} p90 ${quantile(rewound, 0.9).toFixed(2)} max ${Math.max(...rewound, 0).toFixed(2)}, unrewound p50 ${median(present).toFixed(2)}, rewind ${median(offsets.map((o) => o.back))} tics`;
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")} (${said})`, failed: 1 } : { line: `PASS ${label}: ${said}`, failed: 0 };
}

// [rc4l] A side's shots from fua_unlagged_report's JSON, or none.
const report = async (c) => {
  const j = parseConsoleJson(await cap.exec(c, "fua_unlagged_report", cap.SLOW_COMMAND));
  return j && Array.isArray(j.shots) ? j : { shots: [] };
};

// [rc4l] The target strafes one way then the other while the shooter fires, a second each way, for seconds.
async function strafeAndFire(shooter, target, seconds) {
  await cap.exec(shooter, "+attack", { quietMs: 40 });
  try {
    for (let t = 0; t < seconds; t++) {
      const dir = t % 2 ? "moveright" : "moveleft";
      await cap.exec(target, `+${dir}`, { quietMs: 40 });
      await cap.sleep(1000);
      await cap.exec(target, `-${dir}`, { quietMs: 40 });
    }
  } finally {
    await cap.exec(shooter, "-attack", { quietMs: 40 });
  }
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [shooter, target, observer] = set.clients;
  for (const c of [sc, shooter.c]) await cap.exec(c, "fua_unlagged_report reset", { quietMs: 120 });
  await strafeAndFire(shooter.c, target.c, 4);
  await cap.waitTics(sc, 35);
  const serverReport = await report(sc), clientReport = await report(shooter.c);
  const offsets = shotOffsets(serverReport, clientReport, target.player);
  const healthOf = async (c) => ((await c.rpc("world.actors", { players: 1 }))?.actors || []).find((a) => a.player === target.player)?.health;
  return judgeUnlagged(label, { offsets, serverShots: serverReport.shots.length, clientShots: clientReport.shots.length,
    observerHealth: await healthOf(observer.c), serverHealth: await healthOf(sc) });
}

// [rc4l] Cooperative with friendly fire, so each player keeps its own start and a shot still hurts (deathmatch starts are dealt at random and three players on two telefragged one), on loopback and at 100 ms of ping; --side NAME --cvar fua_debug_unlagged_ticoffset=3 shows a screen three tics behind failing it.
const FRIENDLY_FIRE = { teamdamage: 1 };
export const CONFIGS = [
  { name: "lan", players: 2, observers: 1, serverCvars: FRIENDLY_FIRE },
  { name: "ping100", players: 2, observers: 1, lag: "100", serverCvars: FRIENDLY_FIRE },
];

runScenarioIfMain(import.meta.url, () => ({ name: "unlaggedhit", wad: buildUnlaggedHitWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
