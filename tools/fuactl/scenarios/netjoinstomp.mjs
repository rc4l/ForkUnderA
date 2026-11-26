// [Doruk-Aksoy] A hosted map whose every player start holds a monster, so each joiner, its own start blocked, spawns on another and telefrags what stands there: exit 0 when the server stomped a monster during the late joins and every client, the joiners too, holds each corpse exactly where the server does, on loopback and at 100 ms of ping.
//
// usage:  node scenarios/netjoinstomp.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import { clientAgreement } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";

// [Doruk-Aksoy] A friendly arachnotron that never walks and whose corpse stays solid, so a stomped start stays blocked and every later joiner again lands on one that is.
export const TARGET = "ZXStompTarget", TARGET_NUM = 20900;
export const TARGET_DECORATE = `actor ${TARGET} : Arachnotron ${TARGET_NUM}\n{\n  Speed 0\n  +FRIENDLY\n  States\n  {\n  Death:\n    BSPI J 20 A_Scream\n    BSPI KLMNO 7\n    BSPI P -1\n    Stop\n  }\n}\n`;

// [Doruk-Aksoy] Player starts 1-8 on a grid 256 apart in one room, each under a target: far enough apart that a spawn stomps only its own spot's.
export const STARTS = [1, 2, 3, 4, 4001, 4002, 4003, 4004].map((type, i) => ({ type, x: 192 + 256 * (i % 4), y: 192 + 256 * Math.floor(i / 4) }));
export const LATE_JOINS = 3;

const flats = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };

export function buildJoinStompWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1152, 640, flats, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const s of STARTS) {
    m.thing({ x: s.x, y: s.y, angle: 90, type: s.type });
    m.thing({ x: s.x, y: s.y, angle: 90, type: TARGET_NUM });
  }
  return wad([...mapLumps("MAP01", m), { name: "DECORATE", data: TARGET_DECORATE }]);
}

// [Doruk-Aksoy] The targets alive in one read of the server's actors and dead in a later one: what a join stomped.
export function stomped(before, after) {
  const alive = new Set(before.filter((a) => a.c === TARGET && a.health > 0).map((a) => a.netid));
  return after.filter((a) => a.c === TARGET && a.health <= 0 && alive.has(a.netid));
}

// [Doruk-Aksoy] Held to the change first: corpses nobody threw agree without proving anything.
export function stompVerdict(label, count) {
  return count ? { line: `PASS ${label}: the late joins stomped ${count} target(s) on the server`, failed: 0 }
    : { line: `FAIL ${label}: no late join stomped a target on the server`, failed: 1 };
}

const actorsOf = async (c) => (await c.rpc("world.actors", { limit: 100000 }))?.actors || [];

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  let count = 0;
  for (let i = 0; i < LATE_JOINS; i++) {
    const before = await actorsOf(sc);
    await addClient(set, { role: "latejoin" });
    await cap.waitTics(sc, 35);
    count += stomped(before, await actorsOf(sc)).length;
  }
  // [Doruk-Aksoy] A stomp's corpse is thrown at up to 32 units a tic and the server's stops dead against the joiner inside it; a client that moved its own a tic first kept that step until the stop was sent.
  await cap.waitTics(sc, 35);
  const verdict = stompVerdict(label, count);
  const agreed = await clientAgreement(sc, set.clients, label);
  return { lines: [verdict.line, ...agreed.lines], failed: verdict.failed + agreed.failed };
}

// [Doruk-Aksoy] --side NAME --cvar fua_debug_netdrop=ID shows a client missing what the server stopped sending.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netjoinstomp", wad: buildJoinStompWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
