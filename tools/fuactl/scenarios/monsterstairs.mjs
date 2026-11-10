// [Doruk-Aksoy] Walkers an Imp and a Mancubus wide, and a stock Imp, Mancubus and Arachnotron, sent to a goal beyond a flight of 16-deep, 16-high steps, under sv_fua_monsterstairs: exit 0 when every one climbs it, the walkers descend it, none walks off a 64-high ledge, and none climbs it on a map whose MAPINFO says fua_nomonsterstairs.
//
// usage:  node scenarios/monsterstairs.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, addToCVar, callFunc, callSpecial, ACSF, PCD } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

const WIDTH = 512;            // [Doruk-Aksoy] The hall runs north along x 0..WIDTH.
const HALL = 512;             // [Doruk-Aksoy] The walkers' end, south of the stairs.
const LANDING = 512;          // [Doruk-Aksoy] The player's end, north of them, always at floor 0.
const RISE = 16, DEPTH = 16;  // [Doruk-Aksoy] Narrower than either walker, so the old rule turns both back.
const LEDGE = 64;
const SECTOR = { heightceiling: 384, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };

// [Doruk-Aksoy] A north-running hall of `floors`, each [depth, floor], its sectors joined by two-sided lines with both sides of every step textured.
export function hall(m, floors) {
  let y = 0;
  const rows = floors.map(([depth, floor]) => { const r = { y0: y, y1: y + depth, s: m.sector({ ...SECTOR, heightfloor: floor }) }; y += depth; return r; });
  const wall = { texturemiddle: "STARTAN2" }, step = { texturebottom: "STEP1" };
  rows.forEach((r, i) => {
    m.wall(0, r.y0, 0, r.y1, r.s, wall);
    m.wall(WIDTH, r.y1, WIDTH, r.y0, r.s, wall);
    if (i === 0) m.wall(WIDTH, r.y0, 0, r.y0, r.s, wall);
    if (i === rows.length - 1) m.wall(0, r.y1, WIDTH, r.y1, r.s, wall);
    else m.opening(WIDTH, r.y1, 0, r.y1, rows[i + 1].s, r.s, step, step);
  });
  return y;
}

// [Doruk-Aksoy] The floors from `from` to the landing at 0, RISE at a time over steps DEPTH deep, the hall and landing included.
export function flight(from) {
  const dir = Math.sign(-from), floors = [[HALL, from]];
  for (let f = from + dir * RISE; f !== 0; f += dir * RISE) floors.push([DEPTH, f]);
  return [...floors, [LANDING, 0]];
}

// [Doruk-Aksoy] Each map's walkers: what each counts at spawn, the height that ends its test and what it then counts, so every map's right answer is 0.
export const MAPS = {
  MAP01: { floors: flight(-128), start: 1, when: "z >= 0", then: -1, title: "Climb" },
  MAP02: { floors: flight(128), start: 1, when: "z <= 0", then: -1, title: "Descend" },
  MAP03: { floors: [[HALL, LEDGE], [LANDING, 0]], start: 0, when: `z < ${LEDGE}`, then: 1, title: "Ledge" },
  MAP04: { floors: flight(-128), start: 0, when: "z >= 0", then: 1, title: "Opted out", mapinfo: "fua_nomonsterstairs" },
  // [Doruk-Aksoy] Stock enemies, the Arachnotron twice a Mancubus's width, judged by their real height rather than a walker's own count.
  MAP05: { floors: flight(-128), enemies: ["DoomImp", "Fatso", "Arachnotron"], title: "Enemies" },
  // [Doruk-Aksoy] The same enemies teleported onto the flight, where a placement reads only the floor at their centre and leaves them sunk below the steps they cover.
  MAP06: { floors: flight(-128), enemies: ["Arachnotron", "Fatso", "Arachnotron"], placed: [500, 540, 588], title: "Placed" },
  // [Doruk-Aksoy] MAP05's walk with no stairs, for a netgame to set the offsets stairs cause against those any walk has (netmonsterstairs.mjs only).
  MAP07: { floors: [[HALL, 0], [LANDING - HALL + 112, 0], [LANDING, 0]], enemies: ["DoomImp", "Fatso", "Arachnotron"], title: "Flat control", control: true },
};

// [Doruk-Aksoy] The maps the single-player scenario judges: every one but the netgame's control.
export const JUDGED_MAPS = Object.keys(MAPS).filter((m) => !MAPS[m].control);

const RADII = [20, 48, 20];
const SCRIPT_FOR = { 1: 2, [-1]: 3 };
// [Doruk-Aksoy] p_acs.cpp's APROP_Friendly.
const APROP_FRIENDLY = 16;
const SPOT_TID = 11, GOAL_TID = 20, WALKER_TID = 30, ENEMY_GOAL_TID = 21;
// [Doruk-Aksoy] Where the stock enemies stop: on the landing, an Arachnotron's radius past the top step.
export const ENEMY_GOAL_Y = HALL + 224;

const className = (map, radius) => `ZXStairWalker${map}R${radius}`;

// [Doruk-Aksoy] A walker that only chases (no attack states), counting once at spawn when its map says so and once when its test ends.
export function walkerDecorate(map, radius) {
  const { start, when, then } = MAPS[map];
  const spawn = start ? `    TROO A 0 NoDelay ACS_ExecuteAlways(${SCRIPT_FOR[start]}, 0)\n` : "";
  return `Actor ${className(map, radius)}
{
  Monster
  Health 1000
  Radius ${radius}
  Height 56
  Speed 8
  +LOOKALLAROUND
  States
  {
  Spawn:
${spawn}  Idle:
    TROO A 10 A_Look
    Loop
  See:
    TROO A 0 A_JumpIf(${when}, "Counted")
    TROO A 2 A_Chase
    Loop
  Counted:
    TROO A 0 ACS_ExecuteAlways(${SCRIPT_FOR[then]}, 0)
  Chase:
    TROO B 2 A_Chase
    Loop
  }
}
`;
}

// [Doruk-Aksoy] Where the player stands on every map: inside the landing whether stairs come before it or not, beside the walkers' goal.
export const PLAYER = { x: 448, y: HALL + 384 };
export const GOAL = { x: 256, y: HALL + 384 };

// [Doruk-Aksoy] Every map the same: the player on the landing facing south, a map spot for each walker in the hall at y 256, and their goal on the landing.
export function buildMap(map) {
  const m = new UdmfMap();
  hall(m, MAPS[map].floors);
  RADII.forEach((_, i) => m.thing({ x: 128 * (i + 1), y: 256, angle: 90, type: THING.MapSpot, id: SPOT_TID + i }));
  m.thing({ ...GOAL, angle: 0, type: THING.PatrolPoint, id: GOAL_TID });
  // [Doruk-Aksoy] Each stock enemy its own goal on the landing, so they stop apart and stand still rather than jostling over one.
  (MAPS[map].enemies || []).forEach((_, i) => m.thing({ x: 128 * (i + 1), y: ENEMY_GOAL_Y, angle: 0, type: THING.PatrolPoint, id: ENEMY_GOAL_TID + i }));
  m.thing({ ...PLAYER, angle: 270, type: 1 });
  // [Doruk-Aksoy] Starts for a cooperative netgame's other players (netmonsterstairs.mjs), on the landing clear of the goal.
  for (const [type, x, y] of COOP_STARTS) m.thing({ x, y, angle: 270, type });
  return m;
}

export const COOP_STARTS = [[2, 352, HALL + 384], [3, 160, HALL + 384], [4, 64, HALL + 384]];

export const COUNTER_CVAR = "zx_stairs_wrong";

const ENEMY_TID = 31, WAVE_TID = 41;
// [Doruk-Aksoy] A script number past the ones the scenario runs, for a person to watch the enemies fight up the stairs.
export const FIGHT_SCRIPT = 5;
// [Doruk-Aksoy] A second wave sent up the flight uncounted, for a netgame to kill on the steps and show a late joiner (netmonsterstairs.mjs).
export const WAVE_SCRIPT = 6;

// [Doruk-Aksoy] The words spawning `strings[1 + i]` at spot i, each with `tid(i)`, facing north.
const spawnAll = (count, tid) => Array.from({ length: count }, (_, i) =>
  [...callFunc(ACSF.SpawnSpotForced, [1 + i, SPOT_TID + i, tid(i), 64]), PCD.DROP]).flat();

// [Doruk-Aksoy] The floor of the step under y.
export function floorUnder(floors, y) {
  let y0 = 0;
  for (const [depth, floor] of floors) { if (y < y0 + depth) return floor; y0 += depth; }
  return floors.at(-1)[1];
}

// [Doruk-Aksoy] The words moving enemy i onto the flight at y `placed[i]` by SetActorPosition, at the floor under its centre, where a teleport leaves it.
// [Doruk-Aksoy] Placed from z 0 instead, each read as on the landing for the tic before it fell, and the test passed on the build it was written to catch.
const placeAll = (floors, placed = [], tid = ENEMY_TID) => placed.flatMap((y, i) => [PCD.PUSHNUMBER, tid + i, PCD.PUSHNUMBER, 128 * (i + 1) * 65536,
  PCD.PUSHNUMBER, y * 65536, PCD.PUSHNUMBER, floorUnder(floors, y) * 65536, PCD.PUSHNUMBER, 0, PCD.SETACTORPOSITION, PCD.DROP]);

// [Doruk-Aksoy] The words making enemies tid..tid+count-1 friendly and sending each to its own goal: in a netgame, where a player's notarget never reaches the server's monsters, they neither shoot the players nor each other and only walk.
const sendToGoals = (count, tid) => Array.from({ length: count }, (_, i) => [PCD.PUSHNUMBER, tid + i, PCD.PUSHNUMBER, APROP_FRIENDLY, PCD.PUSHNUMBER, 1,
  PCD.SETACTORPROPERTY, ...callSpecial(SPECIAL.Thing_SetGoal, [tid + i, ENEMY_GOAL_TID + i, 0, 1])]).flat();

// [Doruk-Aksoy] Script 1 for stock enemies: counts each in, sends each to the goal, then counts each out once GetActorZ finds it on the landing, waiting on them in turn.
function enemyScript(count, floors, placed) {
  const s = new AcsScript().emit(...spawnAll(count, (i) => ENEMY_TID + i), ...placeAll(floors, placed), ...addToCVar(0, count), ...sendToGoals(count, ENEMY_TID));
  for (let i = 0; i < count; i++) {
    s.label(`wait${i}`).emit(PCD.PUSHNUMBER, ENEMY_TID + i, PCD.GETACTORZ, PCD.PUSHNUMBER, 0, PCD.GE).jump(PCD.IFGOTO, `up${i}`)
      .emit(PCD.DELAYDIRECT, 5).jump(PCD.GOTO, `wait${i}`).label(`up${i}`).emit(...addToCVar(0, -1));
  }
  return s.emit(PCD.TERMINATE).words();
}

// [Doruk-Aksoy] Script 1, puked once the level is up and after the launch's `kill monsters`, spawns each walker at its spot facing north and sends them all to the goal, which they walk to under notarget; scripts 2 and 3 add to and take from the counter.
// [Doruk-Aksoy] A map of stock enemies counts them in script 1 itself, and its FIGHT_SCRIPT spawns them with no goal, to chase and shoot whoever plays it.
export function behavior(map) {
  const { enemies, placed, floors } = MAPS[map];
  const strings = [COUNTER_CVAR, ...(enemies ?? RADII.map((r) => className(map, r)))];
  if (enemies) return acsScripts([
    { code: enemyScript(enemies.length, floors, placed), open: false, number: 1 },
    { code: [...spawnAll(enemies.length, (i) => ENEMY_TID + i), ...placeAll(floors, placed), PCD.TERMINATE], open: false, number: FIGHT_SCRIPT },
    { code: [...spawnAll(enemies.length, (i) => WAVE_TID + i), ...placeAll(floors, placed, WAVE_TID), ...sendToGoals(enemies.length, WAVE_TID), PCD.TERMINATE],
      open: false, number: WAVE_SCRIPT },
  ], strings);
  return acsScripts([
    { code: [...spawnAll(RADII.length, () => WALKER_TID), ...callSpecial(SPECIAL.Thing_SetGoal, [WALKER_TID, GOAL_TID, 0, 1]), PCD.TERMINATE], open: false, number: 1 },
    { code: [...addToCVar(0, 1), PCD.TERMINATE], open: false, number: 2 },
    { code: [...addToCVar(0, -1), PCD.TERMINATE], open: false, number: 3 },
  ], strings);
}

// [Doruk-Aksoy] The maps named, their walkers and MAPINFO in one pk3, with `gameinfo` lines added to MAPINFO's gameinfo block.
export function buildMonsterStairsPk3(maps = JUDGED_MAPS, gameinfo = []) {
  const decorate = maps.filter((map) => !MAPS[map].enemies).flatMap((map) => [...new Set(RADII)].map((r) => walkerDecorate(map, r))).join("\n");
  const blocks = maps.map((map) => `map ${map} "${MAPS[map].title}"\n{\n${MAPS[map].mapinfo ? `\t${MAPS[map].mapinfo}\n` : ""}}\n`);
  const mapinfo = (gameinfo.length ? `gameinfo\n{\n${gameinfo.map((l) => `\t${l}\n`).join("")}}\n` : "") + blocks.join("");
  return pk3([
    ...maps.map((map) => ({ name: `maps/${map}.wad`, data: wad(mapLumps(map, buildMap(map), { behavior: behavior(map) })) })),
    { name: "decorate.txt", data: decorate },
    { name: "mapinfo.txt", data: mapinfo },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const VANTAGES = [{ name: "landing", at: { ...PLAYER, z: 0, angle: 270, pitch: 0 } }];

// [Doruk-Aksoy] Walkers still short of where they should be, plus any that went where they should not; below 0 means one counted twice.
export const COUNTER = { command: COUNTER_CVAR, label: "walkers in the wrong place",
  pattern: `"${COUNTER_CVAR}" is "(-?\\d+)"`, ceiling: 0, floor: 0 };

// [Doruk-Aksoy] Fifteen seconds is about four times what the slowest walker takes to reach the landing.
export const HOLD_TICS = 525;

// [Doruk-Aksoy] The walkers come in by `puke 1` once the level is up, so a `--cvar` the run adds is set before they move.
export const SPAWN_WALKERS = ["puke 1"];

export const SIDES = [{ name: "stairs", launchCvars: { ...MUTE, sv_fua_monsterstairs: 1 }, commands: SPAWN_WALKERS }];

runScenarioIfMain(import.meta.url, () => ({ name: "monsterstairs", wad: buildMonsterStairsPk3(JUDGED_MAPS), maps: JUDGED_MAPS,
  vantages: VANTAGES, counter: COUNTER, sides: SIDES, holdTics: HOLD_TICS }), compareCounter);
