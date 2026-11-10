// [Doruk-Aksoy] The monster stairs template: its flights, its walkers' counting, and the MAPINFO each scenario chooses with.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad, readPk3, parseUdmf } from "../src/udmf.mjs";
import { buildMonsterStairsPk3, behavior, flight, walkerDecorate, MAPS, COUNTER, SIDES, VANTAGES, GOAL, SPAWN_WALKERS, FIGHT_SCRIPT, WAVE_SCRIPT, floorUnder, ENEMY_GOAL_Y, JUDGED_MAPS } from "../scenarios/monsterstairs.mjs";

const HALL_END_OF_STAIRS = 624;
import { ACSF, PCD } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { GAMEINFO, SIDES as GAMEINFO_SIDES } from "../scenarios/monsterstairsgameinfo.mjs";

const files = (pk3) => Object.fromEntries(readPk3(pk3).map((f) => [f.name, f.data]));
const textmap = (all, map) => parseUdmf(readWad(all[`maps/${map}.wad`]).find((l) => l.name === "TEXTMAP").data.toString("latin1"));

test("a flight climbs or descends to the landing at 0, 16 at a time over 16-deep steps", () => {
  const up = flight(-128);
  assert.deepEqual(up.map(([, f]) => f), [-128, -112, -96, -80, -64, -48, -32, -16, 0]);
  assert.ok(up.slice(1, -1).every(([d]) => d === 16), "every step is 16 deep, narrower than either walker");
  assert.deepEqual(flight(128).map(([, f]) => f), [128, 112, 96, 80, 64, 48, 32, 16, 0]);
});

test("each map's sectors carry its floors, joined by textured steps, with three walkers and the player on the landing", () => {
  const all = files(buildMonsterStairsPk3(Object.keys(MAPS)));
  for (const [map, { floors }] of Object.entries(MAPS)) {
    const t = textmap(all, map);
    assert.deepEqual(t.sector.map((s) => s.heightfloor), floors.map(([, f]) => f), map);
    const steps = t.linedef.filter((l) => l.twosided);
    assert.equal(steps.length, floors.length - 1, map);
    for (const l of steps) for (const s of [l.sidefront, l.sideback]) assert.equal(t.sidedef[s].texturebottom, "STEP1", map);
    assert.equal(t.thing.filter((th) => th.type === 1).length, 1, map);
    assert.deepEqual(t.thing.filter((th) => th.type === THING.MapSpot).map((th) => th.id), [11, 12, 13], map);
    const enemyGoals = (MAPS[map].enemies || []).map((_, i) => [128 * (i + 1), ENEMY_GOAL_Y, 21 + i]);
    assert.deepEqual(t.thing.filter((th) => th.type === THING.PatrolPoint).map((th) => [th.x, th.y, th.id]), [[GOAL.x, GOAL.y, 20], ...enemyGoals], map);
    // [Doruk-Aksoy] An enemy as wide as an Arachnotron stopping at its goal overlaps no player start, past the top step.
    for (const [x, y] of enemyGoals) {
      assert.ok(y - 64 > HALL_END_OF_STAIRS, map);
      for (const s of t.thing.filter((th) => th.type >= 1 && th.type <= 4)) assert.ok(Math.abs(s.x - x) >= 80 || Math.abs(s.y - y) >= 80, `${map} start ${s.type}`);
    }
    assert.deepEqual(t.thing.filter((th) => th.type >= 2 && th.type <= 4).map((th) => th.type), [2, 3, 4], map);
    assert.equal(t.thing.length, 8 + (MAPS[map].enemies?.length ?? 0), "no walker is placed by the map, where the launch's kill monsters would kill it");
  }
  const ledge = textmap(all, "MAP03");
  assert.equal(ledge.sector[0].heightfloor - ledge.sector[1].heightfloor, 64, "one line drops 64, past MaxDropOffHeight");
});

test("a walker that must arrive counts in at spawn and out when it does; one that must stay counts only if it leaves", () => {
  const climb = walkerDecorate("MAP01", 20);
  assert.match(climb, /NoDelay ACS_ExecuteAlways\(2, 0\)/);
  assert.match(climb, /A_JumpIf\(z >= 0, "Counted"\)/);
  assert.match(climb, /Counted:\n\s+TROO A 0 ACS_ExecuteAlways\(3, 0\)/);
  const stay = walkerDecorate("MAP03", 48);
  assert.doesNotMatch(stay, /NoDelay/);
  assert.match(stay, /Radius 48/);
  assert.match(stay, /A_JumpIf\(z < 64, "Counted"\)/);
  assert.match(stay, /Counted:\n\s+TROO A 0 ACS_ExecuteAlways\(2, 0\)/);
  assert.doesNotMatch(climb + stay, /Missile:|Melee:/, "walkers only chase");
});

// [Doruk-Aksoy] A locked launch runs kill monsters and notarget, so walkers placed by the map died at once and none chased the player: the first run passed with the fix off.
test("script 1 spawns each walker at its spot and sends them all to the goal, and the side pukes it once the level is up", () => {
  const words = behavior("MAP01");
  // [Doruk-Aksoy] ACS0's code runs from byte 8 to the directory offset the header names.
  const code = Array.from({ length: (words.readInt32LE(4) - 8) / 4 }, (_, i) => words.readInt32LE(8 + 4 * i));
  const spawns = code.reduce((n, w, i) => n + (w === PCD.CALLFUNC && code[i + 2] === ACSF.SpawnSpotForced ? 1 : 0), 0);
  assert.equal(spawns, 3);
  const goal = code.findIndex((w, i) => w === PCD.LSPEC1 + 3 && code[i + 1] === SPECIAL.Thing_SetGoal);
  assert.ok(goal > 0, "Thing_SetGoal(walkers, goal, 0, chasegoal)");
  assert.deepEqual([code[goal - 7], code[goal - 5], code[goal - 3], code[goal - 1]], [30, 20, 0, 1]);
  for (const s of [...SIDES, ...GAMEINFO_SIDES]) assert.deepEqual(s.commands, SPAWN_WALKERS);
  assert.deepEqual(SPAWN_WALKERS, ["puke 1"]);
});

// [Doruk-Aksoy] ACS0 code words from byte 8 to the directory, and the words of each script entry by number.
function scriptsOf(lump) {
  const dir = lump.readInt32LE(4), count = lump.readInt32LE(dir);
  const entries = Array.from({ length: count }, (_, i) => ({ number: lump.readInt32LE(dir + 4 + 12 * i) % 1000, at: lump.readInt32LE(dir + 8 + 12 * i) }));
  const ends = [...entries.map((e) => e.at).slice(1), dir];
  return Object.fromEntries(entries.map((e, i) => [e.number, Array.from({ length: (ends[i] - e.at) / 4 }, (_, k) => lump.readInt32LE(e.at + 4 * k))]));
}
const calls = (code, fn) => code.filter((w, i) => w === PCD.CALLFUNC && code[i + 2] === fn).length;
const specials = (code, special) => code.filter((w, i) => w >= PCD.LSPEC1 && w <= PCD.LSPEC1 + 4 && code[i + 1] === special).length;

test("the enemies map spawns stock monsters, counts them in, sends each to the goal and counts each out by its own height", () => {
  assert.deepEqual(MAPS.MAP05.enemies, ["DoomImp", "Fatso", "Arachnotron"]);
  const s = scriptsOf(behavior("MAP05"));
  assert.equal(calls(s[1], ACSF.SpawnSpotForced), 3);
  assert.equal(specials(s[1], SPECIAL.Thing_SetGoal), 3);
  // [Doruk-Aksoy] A player's notarget never reaches a netgame server's monsters, so they are made friendly instead and only walk.
  assert.equal(s[1].filter((w, i) => w === PCD.SETACTORPROPERTY && s[1][i - 3] === 16 && s[1][i - 1] === 1).length, 3);
  assert.equal(s[1].filter((w) => w === PCD.GETACTORZ).length, 3);
  assert.equal(s[1].filter((w, i) => w === PCD.GE && s[1][i - 1] === 0).length, 3, "on the landing means z >= 0");
  assert.deepEqual(Object.keys(s).map(Number).sort(), [1, FIGHT_SCRIPT, WAVE_SCRIPT]);
  // [Doruk-Aksoy] The second wave walks the same way under its own tids and counts nothing, so a netgame can kill it on the steps.
  assert.equal(calls(s[WAVE_SCRIPT], ACSF.SpawnSpotForced), 3);
  assert.equal(specials(s[WAVE_SCRIPT], SPECIAL.Thing_SetGoal), 3);
  assert.equal(s[WAVE_SCRIPT].filter((w, i) => w === PCD.SETACTORPROPERTY && s[WAVE_SCRIPT][i - 3] === 16).length, 3);
  assert.ok(!s[WAVE_SCRIPT].includes(PCD.GETACTORZ) && !s[WAVE_SCRIPT].includes(ACSF.SetCVar));
  const waveTids = s[WAVE_SCRIPT].filter((w, i) => w === PCD.CALLFUNC && s[WAVE_SCRIPT][i + 2] === ACSF.SpawnSpotForced).length && [41, 42, 43];
  for (const t of waveTids) assert.ok(s[WAVE_SCRIPT].includes(t), `tid ${t}`);
  assert.equal(calls(s[FIGHT_SCRIPT], ACSF.SpawnSpotForced), 3);
  assert.equal(specials(s[FIGHT_SCRIPT], SPECIAL.Thing_SetGoal), 0, "the watched fight has no goal, so they chase the player");
  assert.doesNotMatch(files(buildMonsterStairsPk3())["decorate.txt"].toString(), /MAP05/);
});

// [Doruk-Aksoy] A teleported Arachnotron sat at the floor under its centre, below the steps its box covered, and never moved again.
test("the placed map moves each enemy onto the flight by SetActorPosition, in the scenario and in the fight alike", () => {
  const s = scriptsOf(behavior("MAP06"));
  for (const code of [s[1], s[FIGHT_SCRIPT]]) {
    const at = code.flatMap((w, i) => (w === PCD.SETACTORPOSITION ? [[code[i - 9], code[i - 7] / 65536, code[i - 5] / 65536, code[i - 3] / 65536]] : []));
    // [Doruk-Aksoy] Placed from z 0, each read as on the landing for a tic before it fell, so the test passed on the build it was written to catch.
    assert.deepEqual(at, [[31, 128, 500, -128], [32, 256, 540, -96], [33, 384, 588, -48]]);
  }
  assert.equal(floorUnder(MAPS.MAP06.floors, 624), 0);
  for (const y of MAPS.MAP06.placed) assert.ok(y > 512 - 64 && y < 624 + 64, "every placement's box covers steps");
  assert.equal(scriptsOf(behavior("MAP05"))[1].includes(PCD.SETACTORPOSITION), false);
});

// [Doruk-Aksoy] The flat control is the netgame's: the single-player scenario has nothing to learn from a walk with no stairs.
test("the flat control walks MAP05's enemies the same distance with no stairs, and only the netgame loads it", () => {
  assert.deepEqual(JUDGED_MAPS, ["MAP01", "MAP02", "MAP03", "MAP04", "MAP05", "MAP06"]);
  assert.deepEqual(MAPS.MAP07.enemies, MAPS.MAP05.enemies);
  assert.ok(MAPS.MAP07.floors.every(([, f]) => f === 0));
  const length = (floors) => floors.reduce((y, [d]) => y + d, 0);
  assert.equal(length(MAPS.MAP07.floors), length(MAPS.MAP05.floors));
  assert.equal(length(MAPS.MAP07.floors.slice(0, -1)), length(MAPS.MAP05.floors.slice(0, -1)), "its landing starts where MAP05's does");
  assert.ok(!Object.keys(files(buildMonsterStairsPk3())).includes("maps/MAP07.wad"));
});

test("each map has its own walker classes, and only MAP04 opts out in MAPINFO", () => {
  const all = files(buildMonsterStairsPk3());
  const classes = [...all["decorate.txt"].toString().matchAll(/^Actor (\w+)$/gm)].map((m) => m[1]);
  assert.equal(classes.length, 8);
  assert.equal(new Set(classes).size, 8);
  const mapinfo = all["mapinfo.txt"].toString();
  assert.equal((mapinfo.match(/fua_nomonsterstairs/g) ?? []).length, 1);
  assert.match(mapinfo, /map MAP04 "Opted out"\n\{\n\tfua_nomonsterstairs\n\}/);
  assert.doesNotMatch(mapinfo, /gameinfo/);
});

test("the cvar scenario turns it on from the server; the GAMEINFO one leaves the cvar off and turns it on in gameinfo", () => {
  assert.equal(SIDES[0].launchCvars.sv_fua_monsterstairs, 1);
  assert.equal(GAMEINFO_SIDES[0].launchCvars.sv_fua_monsterstairs, 0);
  const all = files(buildMonsterStairsPk3(["MAP01"], GAMEINFO));
  assert.match(all["mapinfo.txt"].toString(), /^gameinfo\n\{\n\tfua_monsterstairs = true\n\}\nmap MAP01/);
  assert.deepEqual(Object.keys(all).filter((n) => n.startsWith("maps/")), ["maps/MAP01.wad"]);
});

test("the counter passes only at exactly 0", () => {
  assert.equal(COUNTER.ceiling, 0);
  assert.equal(COUNTER.floor, 0);
  assert.equal(new RegExp(COUNTER.pattern).exec('"zx_stairs_wrong" is "-1"')[1], "-1");
});

// [Doruk-Aksoy] The ledge map has no steps, so its landing ends 112 sooner; a vantage past it stood the player in the wall.
test("the player stands inside every map's landing, at floor 0", () => {
  const all = files(buildMonsterStairsPk3(Object.keys(MAPS)));
  for (const [map, { floors }] of Object.entries(MAPS)) {
    const landingStart = floors.slice(0, -1).reduce((y, [d]) => y + d, 0);
    const landingEnd = landingStart + floors.at(-1)[0];
    const { x, y } = VANTAGES[0].at;
    assert.ok(y > landingStart + 56 && y < landingEnd - 56 && x > 56 && x < 512 - 56, map);
    const start = textmap(all, map).thing.find((th) => th.type === 1);
    assert.deepEqual([start.x, start.y], [x, y], map);
  }
});
