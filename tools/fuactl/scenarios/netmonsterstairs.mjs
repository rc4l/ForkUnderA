// [Doruk-Aksoy] monsterstairs.mjs's stock enemies climbing a flight narrower than themselves (MAP05) and lifted off it after a teleport (MAP06), hosted with a player and an observer, sv_fua_monsterstairs turned on once they joined and a third client joining after the climb: exit 0 when every client gets the cvar, never loses an enemy or its health during the climb, never holds one below the steps under it, and holds the server's world exactly once they lie dead where they stopped, and again, a second late joiner too, after a second wave is killed on the steps (each client's worst offset mid-climb is reported, with a flat MAP07 to read it against), on loopback, at 100 ms of ping and with the observer losing a tenth of its packets.
//
// usage:  node scenarios/netmonsterstairs.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import { clientAgreement, netdiff } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";
import { buildMonsterStairsPk3, COUNTER_CVAR, MAPS as STAIR_MAPS, WAVE_SCRIPT } from "./monsterstairs.mjs";

// [Doruk-Aksoy] The enemies climbing, lifted off a teleport, and walking a flat floor as the control any offset is set against.
export const MAPS = ["MAP05", "MAP06", "MAP07"];

// [Doruk-Aksoy] Long enough that even the flat control, whose enemies count as arrived at once, is compared all the way to its goals.
export const MIN_WALK_MS = 6000;

// [Doruk-Aksoy] Every difference netdiff finds is wanted while climbing, so the offsets can be reported rather than cut at a tolerance.
const SEE_EVERY_OFFSET = { pos: 0, angle: 360, health: 0, height: 0.5 };

// [Doruk-Aksoy] Each enemy's radius, for the floor under its box.
export const RADIUS = Object.freeze({ DoomImp: 20, Fatso: 48, Arachnotron: 64 });

// [Doruk-Aksoy] The highest floor under a box spanning y - r to y + r on a north-running map of `floors` ([depth, floor] each), or null when an edge lies within a unit of a step, where rounding decides which side it is on.
export function boxFloor(floors, y, r) {
  let y0 = 0, best = -Infinity;
  for (let i = 0; i < floors.length; i++) {
    const [depth, floor] = floors[i], y1 = y0 + depth;
    if (i > 0 && floors[i - 1][1] !== floor && (Math.abs(y - r - y0) < 1 || Math.abs(y + r - y0) < 1)) return null;
    if (y0 < y + r && y1 > y - r) best = Math.max(best, floor);
    y0 = y1;
  }
  return best;
}

// [Doruk-Aksoy] The enemies a client holds on a floor below both the one under their box and the height they are at: the floor under their centre, which gravity then drops them to, below where the server holds them.
export function sunkEnemies(actors, floors) {
  return actors.filter((a) => RADIUS[a.c] !== undefined).flatMap((a) => {
    const box = boxFloor(floors, a.y, RADIUS[a.c]);
    return box !== null && a.fz < Math.min(box, a.z) - 1 ? [`${a.c} ${a.netid}${a.health <= 0 ? " (dead)" : ""} at y ${a.y.toFixed(0)} on floor ${a.fz.toFixed(0)} under a box floor of ${box} at z ${a.z.toFixed(0)}`] : [];
  });
}

// [Doruk-Aksoy] Each client's sunk enemies over the whole run as one verdict.
export function sunkVerdict(label, found) {
  return found.length ? { line: `FAIL ${label}: ${found.length} time(s) an enemy sank below its steps: ${[...new Set(found)].slice(0, 4).join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: no enemy ever held below the steps under it`, failed: 0 };
}

// [Doruk-Aksoy] How many of the second wave must stand on the steps before it is killed, and how long to wait for them: MAP06 places them there, MAP05 walks them from the hall.
export const WAVE_ON_STEPS = 2, WAVE_WAIT_MS = 8000;

// [Doruk-Aksoy] The living enemies standing on the stairs, between the hall's floor and the landing's.
export function stairWalkers(actors, floors) {
  const low = Math.min(...floors.map(([, f]) => f)), high = Math.max(...floors.map(([, f]) => f));
  return actors.filter((a) => RADIUS[a.c] !== undefined && a.health > 0 && a.z > low && a.z < high);
}

// [Doruk-Aksoy] The dead enemies lying on the stairs, between the hall's floor and the landing's, which a late joiner must then load where the server holds them.
export function stairCorpses(actors, floors) {
  const low = Math.min(...floors.map(([, f]) => f)), high = Math.max(...floors.map(([, f]) => f));
  return actors.filter((a) => RADIUS[a.c] !== undefined && a.health <= 0 && a.z > low && a.z < high);
}

// [Doruk-Aksoy] Whether a console answer says the cvar is on.
export function cvarOn(text, name = "sv_fua_monsterstairs") {
  return new RegExp(`"${name}" is "(true|1)"`).test(String(text));
}

// [Doruk-Aksoy] The counter's value in a console answer, or null when the engine never printed it.
export function counterValue(text) {
  const m = new RegExp(`"${COUNTER_CVAR}" is "(-?\\d+)"`).exec(String(text));
  return m ? Number(m[1]) : null;
}

// [Doruk-Aksoy] A client's rounds of differences during the climb, on the named enemy classes: one missing or at another class or health fails it, and each one's worst offset is reported, not judged.
// [Doruk-Aksoy] A client walks a monster along its direction between the server's corrections, so walking offsets reach 72 units on a flat floor (MAP07) and grew to 108 only when the cvar never arrived: a limit on them cannot tell the two apart reliably.
export function climbVerdict(label, rounds, classes) {
  const all = rounds.flat().filter((d) => classes.some((c) => d.what.startsWith(`${c} `)));
  if (!rounds.length) return { line: `FAIL ${label}: never compared during the climb`, failed: 1 };
  const bad = all.filter((d) => d.apart === undefined && !/ facing /.test(d.what));
  if (bad.length) return { line: `FAIL ${label}: during the climb ${[...new Set(bad.map((d) => d.what))].slice(0, 6).join("; ")}`, failed: 1 };
  const worst = {};
  for (const d of all.filter((x) => x.apart !== undefined)) {
    const name = d.what.slice(0, d.what.indexOf(" units apart")).replace(/ [\d.]+$/, "");
    worst[name] = Math.max(worst[name] ?? 0, d.apart);
  }
  const report = Object.entries(worst).map(([n, v]) => `${n} ${v.toFixed(0)}`).join(", ") || "none apart";
  return { line: `PASS ${label}: ${rounds.length} comparisons during the climb, every enemy present at its health; worst offsets ${report}`, failed: 0 };
}

// [Doruk-Aksoy] Each client that does not hold the cvar on, named.
export function cvarVerdict(label, answers) {
  const off = answers.filter((a) => !cvarOn(a.text)).map((a) => a.who);
  return off.length ? { line: `FAIL ${label}: sv_fua_monsterstairs never reached ${off.join(", ")}`, failed: 1 }
    : { line: `PASS ${label}: sv_fua_monsterstairs on for ${answers.map((a) => a.who).join(", ")}`, failed: 0 };
}

const who = (c) => `${c.role} player ${c.player}`;
const readCvar = async (set) => Promise.all(set.clients.map(async (c) => ({ who: who(c), text: await cap.exec(c.c, "sv_fua_monsterstairs", { quietMs: 200 }) })));

// [Doruk-Aksoy] The answers once every client holds the cvar on, or after `ms`: a reliable command lost on a lossy link arrives only when the client asks for it again, which a single read 20 tics after the change beat.
export async function readCvarSettled(set, ms = 5000, { read = readCvar, sleep = cap.sleep, now = () => Date.now() } = {}) {
  const until = now() + ms;
  for (;;) {
    const answers = await read(set);
    if (answers.every((a) => cvarOn(a.text)) || now() >= until) return answers;
    await sleep(100);
  }
}

export async function judgeNetplay(set, label, { map }) {
  const sc = set.server.c;
  const { enemies, floors } = STAIR_MAPS[map];
  const sunk = new Map();
  const checkFloors = async () => {
    for (const c of set.clients) {
      const r = await c.c.rpc("world.actors", { limit: 100000 });
      sunk.set(c, [...(sunk.get(c) || []), ...sunkEnemies(r?.actors || [], floors)]);
    }
  };
  const lines = [];
  let failed = 0;
  const add = (r) => { lines.push(r.line); failed += r.failed; };
  // [Doruk-Aksoy] Unseen by the enemies, which then only walk to their goals: one stopping to shoot is left walking on a client until the server turns it, which is not the stairs.
  for (const c of set.clients) for (const cheat of ["god", "notarget"]) await cap.exec(c.c, cheat, { quietMs: 120 });
  // [Doruk-Aksoy] On after they joined, so it reaches them as a change (SendSyncedCVar).
  await cap.exec(sc, "sv_fua_monsterstairs 1");
  // [Doruk-Aksoy] A lost reliable packet is noticed only when a later one arrives (the tail loss the netcode overhaul is to fix), so a client still without it now is read again after the climb's traffic.
  let afterChange = await readCvarSettled(set, 1500);
  await cap.exec(sc, "puke 1");
  const rounds = new Map(set.clients.map((c) => [c, []]));
  const start = Date.now(), until = start + 20000;
  let left = null;
  while (Date.now() < until) {
    // [Doruk-Aksoy] A sinking enemy is caught mid-fall, so the floors are read after every comparison rather than once a round.
    for (const c of set.clients) {
      rounds.get(c).push((await netdiff(sc, c.c, { seconds: 0.3, tolerance: SEE_EVERY_OFFSET })).diffs.filter((d) => d.kind === "actor"));
      await checkFloors();
    }
    left = counterValue(await cap.exec(sc, COUNTER_CVAR, { quietMs: 150 }));
    if (left === 0 && Date.now() - start >= MIN_WALK_MS) break;
  }
  if (!afterChange.every((a) => cvarOn(a.text))) afterChange = await readCvarSettled(set);
  add(cvarVerdict(`${label} after the change`, afterChange));
  // [Doruk-Aksoy] Held to the change first: agreeing on enemies that never climbed proves nothing.
  if (left !== 0) add({ line: `FAIL ${label}: on the server ${left ?? "an unknown number"} of the enemies never reached the landing`, failed: 1 });
  for (const c of set.clients) add(climbVerdict(`${label} ${who(c)}`, rounds.get(c), enemies));
  // [Doruk-Aksoy] A joiner after the change gets the cvar at connect (SendSyncedCVarsTo).
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35);
  add(cvarVerdict(`${label} with the late joiner`, await readCvarSettled(set)));
  await checkFloors();
  // [Doruk-Aksoy] At rest: a death goes out with where it died, so every client, the late joiner too, must then hold each enemy exactly; alive, a client keeps the step it walked past the server's stop.
  await cap.exec(sc, "kill monsters");
  await cap.waitTics(sc, 35 * 3);
  await checkFloors();
  for (const c of set.clients) add(sunkVerdict(`${label} ${who(c)} floors`, sunk.get(c) || []));
  const agreed = await clientAgreement(sc, set.clients, label);
  lines.push(...agreed.lines);
  failed += agreed.failed;
  // [Doruk-Aksoy] A second wave killed on the steps, then a second late joiner: a joiner spawned each corpse on the floor under its centre, below the steps the server held it on, which corpses on the landing never showed.
  await cap.exec(sc, `puke ${WAVE_SCRIPT}`);
  const stairs = floors.some(([, f]) => f !== floors[0][1]);
  for (const until = Date.now() + WAVE_WAIT_MS; stairs && Date.now() < until; await cap.waitTics(sc, 2))
    if (stairWalkers((await sc.rpc("world.actors", { limit: 100000 }))?.actors || [], floors).length >= WAVE_ON_STEPS) break;
  await cap.exec(sc, "kill monsters");
  await cap.waitTics(sc, 35 * 2);
  const onSteps = stairCorpses((await sc.rpc("world.actors", { limit: 100000 }))?.actors || [], floors);
  if (stairs && !onSteps.length) add({ line: `FAIL ${label}: on the server no enemy of the second wave died on the steps`, failed: 1 });
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35 * 2);
  const wave = await clientAgreement(sc, set.clients, `${label} after ${onSteps.length} died on the steps`);
  return { lines: [...lines, ...wave.lines], failed: failed + wave.failed };
}

// [Doruk-Aksoy] The server starts with the cvar off; --side NAME --cvar fua_debug_netdrop=ID on the SetCVar command shows a client left predicting the old rule.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1, serverCvars: { sv_fua_monsterstairs: 0 } },
  { name: "ping100", players: 1, observers: 1, lag: "100", serverCvars: { sv_fua_monsterstairs: 0 } },
  { name: "lossy-observer", players: 1, observers: 1, loss: "0,10", serverCvars: { sv_fua_monsterstairs: 0 } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netmonsterstairs", wad: buildMonsterStairsPk3(MAPS), maps: MAPS, configs: CONFIGS, judgeNetplay }), compareNetplay);
