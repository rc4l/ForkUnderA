// [Doruk-Aksoy] A server's bouncing missile past a post only its clients hold, and one bouncing off a post in a corridor so low it hits the floor or ceiling every tic, hosted with a player and an observer: exit 0 when each client's copy keeps flying on the server's own path, facing its way, on loopback and at 100 ms of ping.
//
// usage:  node scenarios/netbounce.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPTF } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { angleApart } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const MISSILE_SPOT = 10, POST_SPOT = 11;

// [Doruk-Aksoy] Each map: the bouncer's spot and the post's spot (x, y, z), whether only the clients hold the post, and the bouncer's class.
export const MAPS = {
  // [Doruk-Aksoy] A client that stopped the server's missile at the post waited for word of a hit the server never had.
  MAP01: { title: "Client-only post", missile: [128, 256, 4], post: [512, 256, 0], clientPost: true, bouncer: "ZXNetBouncer" },
  // [Doruk-Aksoy] The server's word of its bounce off the post went out at the end of P_ZMovement, which a floor or ceiling bounce every tic never reached; hit off-centre, the bounce turns it about 125 degrees.
  MAP02: { title: "Low corridor", missile: [128, 1074, 4], post: [512, 1056, 0], clientPost: false, bouncer: "ZXNetBouncerLow" },
};

// [Doruk-Aksoy] A 1024x512 room holding the starts, and on MAP02 a closed 1024x64 corridor 16 high north of it.
export function buildMap(map) {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: WALL }));
  if (map === "MAP02") m.room(0, 1024, 1024, 1088, { ...ROOM, heightceiling: 16 }, () => ({ side: WALL }));
  const { missile: [mx, my, mz], post: [px, py, pz] } = MAPS[map];
  m.thing({ x: mx, y: my, height: mz, angle: 0, type: THING.MapSpot, id: MISSILE_SPOT });
  m.thing({ x: px, y: py, height: pz, angle: 0, type: THING.MapSpot, id: POST_SPOT });
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return m;
}

export const DECORATE = `
Actor ZXNetPost
{
  Radius 16
  Height 16
  +SOLID
  States
  {
  Spawn:
    COLU A -1
    Stop
  }
}

Actor ZXNetBouncer
{
  Projectile
  Radius 6
  Height 8
  Damage 0
  BounceType "Hexen"
  BounceFactor 1.0
  WallBounceFactor 1.0
  States
  {
  Spawn:
    BAL1 A -1 NoDelay A_ChangeVelocity(8, 0, 0, CVF_REPLACE)
    Stop
  Death:
    BAL1 A -1
    Stop
  }
}

Actor ZXNetBouncerLow : ZXNetBouncer
{
  States
  {
  Spawn:
    BAL1 A -1 NoDelay A_ChangeVelocity(8, 0, 10, CVF_REPLACE)
    Stop
  }
}
`;

// [Doruk-Aksoy] Script 1, puked on the server, spawns the bouncer (and the post, where the server holds it); script 2, CLIENTSIDE and OPEN, spawns a post each client alone holds.
export function behavior(map) {
  const { clientPost, bouncer } = MAPS[map];
  const spawn = (str, spot) => [...callFunc(ACSF.SpawnSpotForced, [str, spot, 0, 0]), PCD.DROP];
  const server = [...spawn(0, MISSILE_SPOT), ...(clientPost ? [] : spawn(1, POST_SPOT)), PCD.TERMINATE];
  const scripts = [{ code: server, open: false, number: 1 }];
  if (clientPost) scripts.push({ code: [...spawn(1, POST_SPOT), PCD.TERMINATE], number: 2, flags: SCRIPTF.CLIENTSIDE });
  return acsScripts(scripts, [bouncer, "ZXNetPost"], { enhanced: true });
}

export function buildNetBouncePk3() {
  const maps = Object.keys(MAPS);
  return pk3([
    ...maps.map((map) => ({ name: `maps/${map}.wad`, data: wad(mapLumps(map, buildMap(map), { behavior: behavior(map) })) })),
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: maps.map((map) => `map ${map} "${MAPS[map].title}"\n{\n}\n`).join("") },
  ]);
}

// [Doruk-Aksoy] How near the server's path and its facing a client's copy must be; a copy a tic or two behind or ahead is still on it.
export const PATH_UNITS = 2, FACING_DEGREES = 2;

// [Doruk-Aksoy] How far off the server's path a reading may be: a client leaves actor bounces to the server, so the tic before its word arrives the copy is up to a tic's travel past the turn.
export const pathTolerance = (r) => PATH_UNITS + Math.hypot(r.vx ?? 0, r.vy ?? 0, r.vz ?? 0);

// [Doruk-Aksoy] What is wrong with a client's two readings of the bouncer against the server's recorded flight: none held, stopped, off the server's path, or facing another way all along it.
// [Doruk-Aksoy] A path that turns back on itself passes a point both ways, so a reading is on it when any sample that near also faces its way.
export function judgeFollow(history, readings, bouncer) {
  const [first, second] = readings;
  if (!first || !second) return ["holds no bouncer"];
  if (Math.hypot(second.x - first.x, second.y - first.y, second.z - first.z) < 1) return [`its bouncer stopped at (${second.x.toFixed(1)}, ${second.y.toFixed(1)})`];
  const samples = history.map((h) => h.actors.find((a) => a.c === bouncer)).filter(Boolean);
  if (!samples.length) return ["the server held no bouncer while it was read"];
  const faults = [];
  for (const r of readings) {
    const apart = (s) => Math.hypot(s.x - r.x, s.y - r.y, s.z - r.z);
    const near = samples.filter((s) => apart(s) <= pathTolerance(r));
    if (!near.length) faults.push(`its bouncer is ${Math.min(...samples.map(apart)).toFixed(2)} units off the server's path`);
    else if (!near.some((s) => angleApart(s.angle, r.angle) <= FACING_DEGREES)) faults.push(`its bouncer faces ${r.angle} where the server's faced ${near[0].angle}`);
  }
  return faults;
}

// [Doruk-Aksoy] Ten seconds of flight: several passes over the post's spot, each a bounce off it on MAP02.
export const FLIGHT_TICS = 350;

// [Doruk-Aksoy] A server's actors each tic until stop() and half a second more, so the recording runs on past whatever is read while it runs.
export function recordUntilStopped(sc, { sleep = cap.sleep } = {}) {
  const history = [];
  let stopped = false, last = -1;
  const done = (async () => {
    while (!stopped) {
      const r = await sc.rpc("world.actors", { limit: 100000 });
      if (r && r.tic !== last) { history.push({ tic: r.tic, actors: r.actors || [] }); last = r.tic; }
      await sleep(5);
    }
    return history;
  })();
  return { stop: async () => { await sleep(500); stopped = true; return done; } };
}

// [Doruk-Aksoy] Each client read twice, five tics apart, while the server's flight is recorded from before the first reading to after the second.
async function clientFollows(sc, client, map) {
  const { bouncer } = MAPS[map];
  const recording = recordUntilStopped(sc);
  await cap.sleep(300);
  const read = async () => ((await client.c.rpc("world.actors", { limit: 100000 }))?.actors || []).find((a) => a.c === bouncer);
  const first = await read();
  await cap.waitTics(client.c, 5);
  const second = await read();
  return judgeFollow(await recording.stop(), [first, second], bouncer);
}

export async function judgeNetplay(set, label, { map }) {
  const sc = set.server.c;
  await cap.exec(sc, "puke 1");
  await cap.waitTics(sc, FLIGHT_TICS);
  const lines = [];
  let failed = 0;
  for (const c of set.clients) {
    const faults = await clientFollows(sc, c, map);
    const name = `${label} ${c.role} player ${c.player}`;
    lines.push(faults.length ? `FAIL ${name}: ${faults.join("; ")}` : `PASS ${name}: on the server's path`);
    if (faults.length) failed++;
  }
  return { lines, failed };
}

export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netbounce", wad: buildNetBouncePk3(), maps: Object.keys(MAPS), configs: CONFIGS, judgeNetplay }), compareNetplay);
