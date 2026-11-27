// [Doruk-Aksoy] A hosted room of barrels and sleeping zombies, read through a snapshot client's world.actors: exit 0 when at legacy suppression 3 the client lists a mirror for every non-player actor the server holds, and at 2 its mirrors list beside the legacy copies when asked for, each id once by default.
//
// usage:  node scenarios/netmirrors.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";
import { THING } from "../src/doomnums.mjs";
import * as cap from "../src/capture.mjs";

const BARREL = 2035;

export function buildMirrorsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 768, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 128, angle: 90, type });
  for (let i = 0; i < 4; i++) m.thing({ x: 384 + 96 * i, y: 384, angle: 0, type: BARREL });
  // [Doruk-Aksoy] Facing the far wall and deaf, so they never wake and walk.
  for (let i = 0; i < 2; i++) m.thing({ x: 384 + 192 * i, y: 640, angle: 90, type: THING.Zombieman, ambush: true });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] The non-player ids the server holds that `rows` (a client's) never lists, `pick` choosing the rows that count.
export function missingIds(serverActors, rows, pick = () => true) {
  const have = new Set(rows.filter(pick).map((a) => a.netid));
  return serverActors.filter((a) => a.player < 0 && a.netid > 0 && !have.has(a.netid)).map((a) => a.netid);
}

// [Doruk-Aksoy] The ids listed more than once.
export function duplicateIds(rows) {
  const seen = new Set(), twice = new Set();
  for (const a of rows) (seen.has(a.netid) ? twice : seen).add(a.netid);
  return [...twice];
}

// [Doruk-Aksoy] One client's verdict: every server id listed as `want` asks, and no id twice by default.
export function mirrorVerdict(label, { missing, duplicates, what }) {
  const faults = [];
  if (missing.length) faults.push(`${missing.length} of the server's actors not listed ${what} (${missing.slice(0, 6).join(", ")})`);
  if (duplicates.length) faults.push(`ids listed twice by default (${duplicates.slice(0, 6).join(", ")})`);
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 } : { line: `PASS ${label}: every server actor listed ${what}`, failed: 0 };
}

const rowsOf = async (c, extra = {}) => (await c.rpc("world.actors", { limit: 100000, ...extra }))?.actors || [];

export async function judgeNetplay(set, label, { config }) {
  const sc = set.server.c;
  await cap.waitTics(sc, 70);
  const server = await rowsOf(sc);
  const lines = [];
  let failed = 0;
  for (const c of set.clients) {
    const plain = await rowsOf(c.c);
    const r = config.level === 3
      ? mirrorVerdict(`${label} ${c.role} player ${c.player}`, { missing: missingIds(server, plain), duplicates: duplicateIds(plain), what: "as a mirror" })
      : mirrorVerdict(`${label} ${c.role} player ${c.player}`, { missing: missingIds(server, await rowsOf(c.c, { mirrors: 1 }), (a) => a.mirror === 1), duplicates: duplicateIds(plain), what: "as a mirror beside its legacy copy" });
    lines.push(r.line);
    failed += r.failed;
  }
  return { lines, failed };
}

// [Doruk-Aksoy] Level 3, where a mirror is all a client holds, and level 2, the default, where a legacy copy holds each id too.
export const CONFIGS = [
  { name: "level3", players: 1, observers: 1, level: 3, serverCvars: { ...SNAP_SERVER_CVARS, fua_snap_nolegacy: 3 } },
  { name: "level2", players: 1, observers: 1, level: 2, serverCvars: SNAP_SERVER_CVARS },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netmirrors", wad: buildMirrorsWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
