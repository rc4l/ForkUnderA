// [Doruk-Aksoy] A hosted map with nothing moving, where a client throws away the reliable packet carrying a server cvar change and nothing reliable follows: exit 0 when that client, and an observer that lost nothing, hold the change within a few seconds anyway, in snapshot mode on loopback and at 100 ms of ping.
//
// usage:  node scenarios/nettailloss.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";

// [Doruk-Aksoy] A server cvar a server sends its clients when it changes (SVC2_SETCVAR), harmless on a map without monsters.
export const CVAR = "sv_fua_monsterstairs";

// [Doruk-Aksoy] How long a lost tail may take: in snapshot mode it came back within half a second, the next datagram carrying the newest reliable sequence.
export const ARRIVE_MS = 3000;

export function buildTailLossWad() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 192], [2, 576]]) m.thing({ x, y: 256, angle: 90, type });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] The milliseconds until `read` answered `want`, polled every `everyMs`, or null when `ms` passed first.
export async function msUntil(read, want, ms, { everyMs = 100, sleep = cap.sleep, now = () => Date.now() } = {}) {
  const start = now();
  for (;;) {
    if ((await read()) === want) return now() - start;
    if (now() - start >= ms) return null;
    await sleep(everyMs);
  }
}

// [Doruk-Aksoy] One client's verdict: the change held in time, and for the one that dropped, the drop spent, or the run proved nothing; so does a client that already held the value before the change.
export function arrivalVerdict(label, { took, dropLeft = 0, want, before }) {
  if (before === want) return { line: `FAIL ${label}: the client held ${want} before the change, so its arrival proves nothing`, failed: 1 };
  if (dropLeft > 0) return { line: `FAIL ${label}: the client never threw a reliable packet away, so nothing was lost`, failed: 1 };
  if (took === null) return { line: `FAIL ${label}: ${CVAR} never reached ${want} within ${ARRIVE_MS / 1000} s`, failed: 1 };
  return { line: `PASS ${label}: ${CVAR} reached ${want} after ${took} ms`, failed: 0 };
}

const readOn = (c) => async () => cap.echoedCvar(await cap.exec(c, CVAR, { quietMs: 60 }), CVAR);

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [dropper, observer] = set.clients;
  const lines = [];
  let failed = 0;
  // [Doruk-Aksoy] On then off, each change's packet thrown away by the player, so two tails are lost and each must come back on its own.
  for (const want of ["true", "false"]) {
    await cap.waitTics(sc, 70);
    const [before, seenBefore] = [await readOn(dropper.c)(), await readOn(observer.c)()];
    await cap.exec(dropper.c, "fua_debug_droppackets 1", { quietMs: 60 });
    await cap.exec(sc, `${CVAR} ${want === "true" ? 1 : 0}`, { quietMs: 60 });
    const [took, seen] = await Promise.all([msUntil(readOn(dropper.c), want, ARRIVE_MS), msUntil(readOn(observer.c), want, ARRIVE_MS)]);
    const dropLeft = Number(cap.echoedCvar(await cap.exec(dropper.c, "fua_debug_droppackets", { quietMs: 60 }), "fua_debug_droppackets") ?? 1);
    for (const r of [arrivalVerdict(`${label} ${want} player ${dropper.player}`, { took, dropLeft, want, before }), arrivalVerdict(`${label} ${want} observer player ${observer.player}`, { took: seen, want, before: seenBefore })]) {
      lines.push(r.line);
      failed += r.failed;
    }
  }
  return { lines, failed };
}

// [Doruk-Aksoy] A player whose packets are thrown away and an observer whose are not, in snapshot mode, whose every datagram names the newest reliable sequence; in legacy mode nothing does, and the change never came.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1, serverCvars: SNAP_SERVER_CVARS },
  { name: "ping100", players: 1, observers: 1, lag: "100", serverCvars: SNAP_SERVER_CVARS },
];

runScenarioIfMain(import.meta.url, () => ({ name: "nettailloss", wad: buildTailLossWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
