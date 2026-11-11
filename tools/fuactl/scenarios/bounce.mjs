// [Doruk-Aksoy] Bouncing projectiles at a pillar beside a sector divider, dropped on a post and on a monster, run up a slope with walls-only bouncing, and an MBF bouncer spent on a post: exit 0 when each bounces off what it hit, or explodes or dies where its bounces run out.
//
// usage:  node scenarios/bounce.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, addToCVar, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

export const COUNTER_CVAR = "zx_bounce_wrong";
const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const STEP = { texturebottom: "STEP1", texturetop: "STARTAN2" };
const SPOT_TID = 10;

// [Doruk-Aksoy] Counts the actor in at spawn and out at `out`, through scripts 2 and 3.
const COUNT_IN = "ACS_ExecuteAlways(2, 0)";
const COUNT_OUT = "ACS_ExecuteAlways(3, 0)";

// [Doruk-Aksoy] Each map: its title, the actors puke 1 spawns at spots SPOT_TID.. (class, x, y, z), and how the map is built.
export const MAPS = {
  // [Doruk-Aksoy] A missile faster than 16 a tic, whose corner trace misses the pillar, so it reflected off the divider or the face it slid along and was pushed into the pillar.
  MAP01: { title: "Stale line", spawn: [["ZXBounceStale", 500, 200, 64], ["ZXBounceStale", 500, 1224, 64]], build: stalelineMap },
  // [Doruk-Aksoy] Two bounces on a post's top, the second its last, which left it bouncing on forever.
  MAP02: { title: "Spent on a post", spawn: [["ZXBouncePost", 256, 128, 0], ["ZXBounceOnTop", 256, 128, 96]], build: plainMap },
  // [Doruk-Aksoy] Onto a monster's head, which it does not bounce off, so it hovered there.
  MAP03: { title: "Onto a monster", spawn: [["ZXBounceStill", 256, 128, 0], ["ZXBounceOnMonster", 256, 128, 96]], build: plainMap },
  // [Doruk-Aksoy] A walls-only bouncer run into a slope, no line in its way, which pressed into it forever.
  MAP04: { title: "Walls only", spawn: [["ZXBounceWallsOnly", 256, 128, 16]], build: slopeMap },
  // [Doruk-Aksoy] A non-missile MBF bouncer with one bounce, spent on a post, which stopped there instead of dying.
  MAP05: { title: "MBF spent", spawn: [["ZXBouncePost", 400, 128, 0], ["ZXBounceGrenade", 256, 128, 16]], build: plainMap },
};

// [Doruk-Aksoy] A 1024x256 room with the player at its west end.
function plainMap(m) {
  m.room(0, 0, 1024, 256, ROOM, () => ({ side: WALL }));
}

// [Doruk-Aksoy] Two closed lanes 1024 apart, so both share blockmap rows; the second lists its divider before the pillar, as a blockmap walks either order.
export const LANE_DY = 1024;
// [Doruk-Aksoy] The pillar's south face 6 units north of the path at y 200, its west face what the missile runs into, all in one 128-unit blockmap row.
export const PILLAR = { x0: 600, y0: 206, x1: 664, y1: 250 };
// [Doruk-Aksoy] South of y 196 the ceiling is lower but clear of the missile, so the divider narrows the box the path's centre is in without blocking it.
export const DIVIDER_Y = 196;
function stalelineMap(m) {
  for (const [dy, dividerFirst] of [[0, false], [LANE_DY, true]]) {
    const north = m.sector(ROOM), south = m.sector({ ...ROOM, heightceiling: 128 });
    const divider = () => m.opening(0, dy + DIVIDER_Y, 1024, dy + DIVIDER_Y, south, north, STEP, STEP);
    if (dividerFirst) divider();
    m.wall(0, dy, 0, dy + DIVIDER_Y, south, WALL); m.wall(1024, dy + DIVIDER_Y, 1024, dy, south, WALL); m.wall(1024, dy, 0, dy, south, WALL);
    m.wall(0, dy + DIVIDER_Y, 0, dy + 512, north, WALL); m.wall(0, dy + 512, 1024, dy + 512, north, WALL); m.wall(1024, dy + 512, 1024, dy + DIVIDER_Y, north, WALL);
    // [Doruk-Aksoy] The pillar's faces walked clockwise around it, so the room is on each one's right.
    const { x0, y0, x1, y1 } = PILLAR;
    m.wall(x0, dy + y1, x0, dy + y0, north, WALL); m.wall(x0, dy + y0, x1, dy + y0, north, WALL);
    m.wall(x1, dy + y0, x1, dy + y1, north, WALL); m.wall(x1, dy + y1, x0, dy + y1, north, WALL);
    if (!dividerFirst) divider();
  }
}

// [Doruk-Aksoy] A flat west room and an east sector whose floor Plane_Align slopes from 0 at x 512 up to 128 at x 1024.
function slopeMap(m) {
  const west = m.sector(ROOM), east = m.sector({ ...ROOM, heightfloor: 128 });
  m.wall(0, 0, 0, 256, west, WALL); m.wall(0, 256, 512, 256, west, WALL); m.wall(512, 0, 0, 0, west, WALL);
  m.wall(512, 256, 1024, 256, east, WALL); m.wall(1024, 256, 1024, 0, east, WALL); m.wall(1024, 0, 512, 0, east, WALL);
  m.opening(512, 0, 512, 256, east, west, STEP, STEP, { special: SPECIAL.Plane_Align, arg0: 1 });
}

export const PLAYER = { x: 64, y: 64 };

export function buildMap(map) {
  const m = new UdmfMap();
  MAPS[map].build(m);
  MAPS[map].spawn.forEach(([, x, y, height], i) => m.thing({ x, y, height, angle: 0, type: THING.MapSpot, id: SPOT_TID + i }));
  m.thing({ ...PLAYER, angle: 0, type: 1 });
  return m;
}

export const DECORATE = `
Actor ZXBouncePost
{
  Radius 16
  Height 32
  +SOLID
  States
  {
  Spawn:
    COLU A -1
    Stop
  }
}

Actor ZXBounceStill
{
  Monster
  Radius 16
  Height 32
  Health 1000
  States
  {
  Spawn:
    TROO A -1
    Stop
  }
}

Actor ZXBounceStale
{
  Projectile
  Radius 8
  Height 8
  Damage 0
  BounceFactor 1.0
  WallBounceFactor 1.0
  +BOUNCEONWALLS
  +BOUNCEONFLOORS
  +BOUNCEONCEILINGS
  States
  {
  Spawn:
    BAL1 A 0 NoDelay ${COUNT_IN}
    BAL1 A 0 A_ChangeVelocity(20, 0, 0, CVF_REPLACE)
  Fly:
    BAL1 A 1 A_JumpIf(x > ${PILLAR.x0}, "Through")
    BAL1 A 0 A_JumpIf(x < 400, "Away")
    Loop
  Away:
    BAL1 A 0 ${COUNT_OUT}
  Watch:
    BAL1 A 1 A_JumpIf(x > ${PILLAR.x0}, "Through")
    Loop
  Through:
    BAL1 A 0 ${COUNT_IN}
    BAL1 A -1
    Stop
  Death:
    BAL1 A -1
    Stop
  }
}

Actor ZXBounceOnTop
{
  Projectile
  -NOGRAVITY
  +CANPASS
  Radius 6
  Height 8
  Damage 0
  BounceType "Hexen"
  BounceCount 2
  BounceFactor 0.9
  States
  {
  Spawn:
    BAL1 A 0 NoDelay ${COUNT_IN}
  Fly:
    BAL1 A -1
    Stop
  Death:
    BAL1 A 0 ${COUNT_OUT}
    BAL1 A -1
    Stop
  }
}

Actor ZXBounceOnMonster : ZXBounceOnTop
{
  BounceCount 0
}

Actor ZXBounceWallsOnly
{
  Projectile
  Radius 8
  Height 8
  Damage 0
  +BOUNCEONWALLS
  States
  {
  Spawn:
    BAL1 A 0 NoDelay ${COUNT_IN}
    BAL1 A -1 A_ChangeVelocity(8, 0, 0, CVF_REPLACE)
    Stop
  Death:
    BAL1 A 0 ${COUNT_OUT}
    BAL1 A -1
    Stop
  }
}

Actor ZXBounceGrenade
{
  Radius 6
  Height 8
  +NOGRAVITY
  BounceType "Grenade"
  BounceCount 1
  States
  {
  Spawn:
    BAL1 A 0 NoDelay ${COUNT_IN}
    BAL1 A -1 A_ChangeVelocity(8, 0, 0, CVF_REPLACE)
    Stop
  Death:
    BAL1 A 0 ${COUNT_OUT}
    BAL1 A -1
    Stop
  }
}
`;

// [Doruk-Aksoy] Script 1, puked once the level is up and after the launch's kill monsters, spawns the map's actors facing east; scripts 2 and 3 add to and take from the counter.
export function behavior(map) {
  const spawn = MAPS[map].spawn;
  const strings = [COUNTER_CVAR, ...spawn.map(([cls]) => cls)];
  const code = spawn.flatMap((_, i) => [...callFunc(ACSF.SpawnSpotForced, [1 + i, SPOT_TID + i, 0, 0]), PCD.DROP]);
  return acsScripts([
    { code: [...code, PCD.TERMINATE], open: false, number: 1 },
    { code: [...addToCVar(0, 1), PCD.TERMINATE], open: false, number: 2 },
    { code: [...addToCVar(0, -1), PCD.TERMINATE], open: false, number: 3 },
  ], strings);
}

export function buildBouncePk3(maps = Object.keys(MAPS)) {
  return pk3([
    ...maps.map((map) => ({ name: `maps/${map}.wad`, data: wad(mapLumps(map, buildMap(map), { behavior: behavior(map) })) })),
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: maps.map((map) => `map ${map} "${MAPS[map].title}"\n{\n}\n`).join("") },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const VANTAGES = [{ name: "west", at: { ...PLAYER, z: 0, angle: 0, pitch: 0 } }];

// [Doruk-Aksoy] Actors that never did the right thing, each counted in at spawn and out once it did.
export const COUNTER = { command: COUNTER_CVAR, label: "bouncers that did not bounce or end right",
  pattern: `"${COUNTER_CVAR}" is "(-?\\d+)"`, ceiling: 0, floor: 0 };

// [Doruk-Aksoy] Five seconds, several times the longest any case takes to settle.
export const HOLD_TICS = 175;

export const SIDES = [{ name: "bounce", launchCvars: { ...MUTE }, commands: ["puke 1"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "bounce", wad: buildBouncePk3(), maps: Object.keys(MAPS),
  vantages: VANTAGES, counter: COUNTER, sides: SIDES, holdTics: HOLD_TICS }), compareCounter);
