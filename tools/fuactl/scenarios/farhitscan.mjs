// [rc4l] A player firing a pistol down a hall at 400 zombiemen 6,300 to 7,300 units away (the player's hitscan reaches 8,192) while an observer watches: exit 0 when, shot for shot, the server's trace hits a monster, the shooter's own predicted trace hits the same one, and every monster the server hit holds the same health on the shooter and on the observer, with the horde asleep on loopback and at 100 ms, and awake and marching at 100 ms.
//
// usage:  node scenarios/farhitscan.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";
import { parseConsoleJson } from "../src/telemetry.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] The horde: COLUMNS by ROWS zombiemen, NEAR to NEAR + (COLUMNS - 1) * SPACING units east of the shooter, one row on its line of fire.
export const COLUMNS = 20, ROWS = 20, SPACING = 48, NEAR = 6400, SHOOTER_Y = 512;
export const SHOTS = 16;

// [rc4l] One long hall: the shooter's start and the observer's at the west end facing east, and the horde at the far end, either asleep behind two sound-blocking lines (a gunshot wakes every monster that hears it and can see the shooter, deaf or not) so only a hit wakes one, or facing the players to wake and march.
export function buildHall({ deaf }) {
  const m = new UdmfMap();
  const floor = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
  const hall = m.room(0, 0, NEAR + COLUMNS * SPACING + 256, 1024, floor, () => ({ side: { texturemiddle: "STARTAN2" } }));
  if (deaf) {
    const quiet = () => ({ blocksound: true });
    const ring = m.block(NEAR - 160, 4, NEAR + COLUMNS * SPACING + 96, 1020, floor, hall, () => ({}), () => ({}), quiet);
    m.block(NEAR - 96, 8, NEAR + COLUMNS * SPACING + 64, 1016, floor, ring, () => ({}), () => ({}), quiet);
  }
  m.thing({ x: 128, y: SHOOTER_Y, angle: 0, type: 1 });
  m.thing({ x: 128, y: 160, angle: 0, type: 2 });
  for (let c = 0; c < COLUMNS; c++) {
    for (let r = 0; r < ROWS; r++) {
      m.thing({ x: NEAR + c * SPACING, y: SHOOTER_Y + (r - ROWS / 2) * SPACING, angle: deaf ? 0 : 180, type: THING.Zombieman, ...(deaf ? { ambush: true } : {}) });
    }
  }
  return m;
}

// [rc4l] MAP01's horde asleep, MAP02's awake.
export function buildFarHitscanWad() {
  return wad([...mapLumps("MAP01", buildHall({ deaf: true })), ...mapLumps("MAP02", buildHall({ deaf: false }))]);
}

// [rc4l] Each server shot matched by number with the shooter's: what each side's trace hit.
export function matchShots(serverReport, clientReport) {
  const byN = new Map((clientReport?.shots || []).map((s) => [s.n, s]));
  return (serverReport?.shots || []).map((s) => {
    const c = byN.get(s.n);
    return { n: s.n, server: s.hitNetId ?? -1, client: c ? (c.traced ? c.hitNetId : null) : undefined };
  });
}

// [rc4l] The verdict: how often the server hit a monster and the shooter's trace the same one, and every monster hit holding the server's health on both clients.
export function judgeFarHits(label, { shots, health, bars }) {
  const faults = [];
  const hits = shots.filter((s) => s.server >= 0);
  const traced = shots.filter((s) => s.client !== null && s.client !== undefined);
  const agreed = shots.filter((s) => s.client === s.server);
  const rate = (n) => (shots.length ? n / shots.length : 0);
  if (shots.length < SHOTS - 2) faults.push(`the server counted ${shots.length} shots of ${SHOTS}`);
  if (traced.length < shots.length) faults.push(`the shooter traced ${traced.length} of ${shots.length} shots (cl_clientsidepuffs)`);
  if (rate(hits.length) < bars.hit) faults.push(`the server hit a monster with ${hits.length} of ${shots.length} shots`);
  if (rate(agreed.length) < bars.agree) faults.push(`the shooter's trace hit what the server's did on ${agreed.length} of ${shots.length} shots`);
  const ids = [...new Set(hits.map((s) => s.server))];
  const off = [];
  for (const id of ids) {
    const h = health.get(id) || {};
    for (const who of ["shooter", "observer"]) if (h[who] !== h.server) off.push(`${who} holds monster ${id} at ${h[who]}, the server at ${h.server}`);
  }
  if (off.length) faults.push(off.slice(0, 6).join("; ") + (off.length > 6 ? `; ${off.length - 6} more` : ""));
  const killed = ids.filter((id) => (health.get(id)?.server ?? 1) <= 0).length;
  const said = `${shots.length} shots at 6,400+ units, ${hits.length} hit, ${agreed.length} traced alike on the shooter, ${ids.length} monsters hit (${killed} killed), each held alike by the shooter and the observer`;
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")} (${said})`, failed: 1 } : { line: `PASS ${label}: ${said}`, failed: 0 };
}

const report = async (c) => {
  const j = parseConsoleJson(await cap.exec(c, "fua_unlagged_report", cap.SLOW_COMMAND));
  return j && Array.isArray(j.shots) ? j : { shots: [] };
};

// [rc4l] Every actor's health by network id.
const healthById = async (c) => new Map(((await c.rpc("world.actors", { limit: 100000 }))?.actors || []).map((a) => [a.netid, a.health]));

export async function judgeNetplay(set, label, { config }) {
  const sc = set.server.c;
  const [shooter, observer] = set.clients;
  for (const c of set.clients) await cap.exec(c.c, "god", { quietMs: 120 });
  await cap.waitTics(sc, 35);
  for (const c of [sc, shooter.c]) await cap.exec(c, "fua_unlagged_report reset", { quietMs: 120 });
  // [rc4l] Single taps a weapon cycle apart, so every shot is the pistol's accurate first one.
  for (let i = 0; i < SHOTS; i++) {
    await cap.exec(shooter.c, "+attack", { quietMs: 30 });
    await cap.sleep(60);
    await cap.exec(shooter.c, "-attack", { quietMs: 30 });
    await cap.sleep(600);
  }
  await cap.waitTics(sc, 70);
  const shots = matchShots(await report(sc), await report(shooter.c));
  const [server, mine, theirs] = [await healthById(sc), await healthById(shooter.c), await healthById(observer.c)];
  const health = new Map([...new Set(shots.map((s) => s.server))].map((id) => [id, { server: server.get(id), shooter: mine.get(id), observer: theirs.get(id) }]));
  return judgeFarHits(label, { shots, health, bars: config.bars });
}

const SHOOTER = { cl_clientsidepuffs: 1 };
// [rc4l] The horde asleep (only a hit wakes one) on loopback and at 100 ms, and awake and marching at 100 ms, where a monster walks between the two traces since unlagged rewinds only players (12 and 14 of 16 agreed in measured runs, the asleep horde 16).
export const CONFIGS = [
  { name: "asleep-lan", maps: ["MAP01"], players: 1, observers: 1, clientCvars: SHOOTER, bars: { hit: 0.9, agree: 0.9 } },
  { name: "asleep-ping100", maps: ["MAP01"], players: 1, observers: 1, lag: "100", clientCvars: SHOOTER, bars: { hit: 0.9, agree: 0.9 } },
  { name: "awake-ping100", maps: ["MAP02"], players: 1, observers: 1, lag: "100", clientCvars: SHOOTER, bars: { hit: 0.75, agree: 0.5 } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "farhitscan", wad: buildFarHitscanWad(), maps: ["MAP01", "MAP02"], configs: CONFIGS, judgeNetplay }), compareNetplay);
