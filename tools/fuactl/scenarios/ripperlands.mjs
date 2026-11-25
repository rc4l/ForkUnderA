// [MGOOOOOO] Two bouncing rippers aimed down into a monster touching the player (MM8BDM Cut Man's point-blank slices): exit 0 when both rip on through it, as on Zandronum, instead of exploding where they come down on it.
//
// usage:  node scenarios/ripperlands.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

// [MGOOOOOO] Cut Man's last two slices (angle, side offset, height, pitch) into a dummy that is a monster, since a ripper bounces off a non-monster; each slice leaves a marker where it dies.
export const DECORATE = `actor ZXRipSlice
{
  Projectile
  +RIPPER
  Bouncetype Hexen
  Bouncecount 2
  Damage (0)
  Radius 5
  Height 5
  Speed 35
  States
  {
  Spawn:
    TNT1 A -1
    Stop
  Death:
    TNT1 A 0 A_SpawnItemEx("ZXRipDied")
    Stop
  }
}
actor ZXRipDied
{
  +NOINTERACTION
  States
  {
  Spawn:
    TNT1 A -1
    Stop
  }
}
actor ZXRipDummy 31117
{
  +SOLID
  +SHOOTABLE
  +ISMONSTER
  Health 1000
  Radius 16
  Height 56
  States
  {
  Spawn:
    TNT1 A -1
    Stop
  }
}
actor ZXRipSlices : CustomInventory
{
  States
  {
  Pickup:
    TNT1 A 0 A_FireCustomMissile("ZXRipSlice", 5, 0, -2, -2, 0, -5)
    TNT1 A 0 A_FireCustomMissile("ZXRipSlice", 15, 0, -8, -6, 0, -10)
    Stop
  }
}
`;

export const SLICES = 2;
export const DUMMY = { x: 256, y: 164, radius: 16, height: 56 };

export function buildRipperLandsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  m.thing({ x: DUMMY.x, y: DUMMY.y, angle: 270, type: 31117 });
  return wad([...mapLumps("MAP01", m), { name: "DECORATE", data: DECORATE }]);
}

// [MGOOOOOO] Monsters on, since the dummy is one.
export const SIDES = [{ name: "vk", noGl: true, launchCvars: { sv_nomonsters: 0 } }];

// [MGOOOOOO] Where the slices died (their markers), and whether the dummy stands, from world.actors.
export function readDeaths(actors) {
  const list = actors || [];
  return { deaths: list.filter((a) => a.c === "ZXRipDied"), dummy: list.some((a) => a.c === "ZXRipDummy") };
}

// [MGOOOOOO] A death inside the dummy's box (a slice's radius around it) is a slice that exploded on it.
export function diedInDummy(d) {
  const reach = DUMMY.radius + 5;
  return Math.abs(d.x - DUMMY.x) <= reach && Math.abs(d.y - DUMMY.y) <= reach && d.z <= DUMMY.height + 5;
}

export function judgeRipperLands(label, { deaths, dummy }) {
  if (!dummy) return { line: `FAIL ${label}: the monster dummy is not there, so nothing was tested`, failed: 1 };
  const inside = deaths.filter(diedInDummy).length;
  if (inside) return { line: `FAIL ${label}: ${inside} of ${SLICES} slices exploded on the monster they came down into`, failed: 1 };
  if (deaths.length !== SLICES) return { line: `FAIL ${label}: ${deaths.length} of ${SLICES} slices died in the time given, so the test is inconclusive`, failed: 1 };
  return { line: `PASS ${label}: both slices ripped on through the monster they came down into, dying elsewhere`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  await c.rpc("sim.resume", {}).catch(() => {});
  await cap.waitTics(c, 35);
  await cap.exec(c, "give ZXRipSlices");
  await cap.waitTics(c, 105);
  return judgeRipperLands(label, readDeaths((await c.rpc("world.actors", { all: true }))?.actors));
}

runScenarioIfMain(import.meta.url, () => ({ name: "ripperlands", wad: buildRipperLandsWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
