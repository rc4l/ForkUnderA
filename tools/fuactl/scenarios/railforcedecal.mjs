// [rc4l] A rail whose weapon has no decal, firing a FORCEDECAL puff that has one (zandronum@deffd2fbd792): exit 0 when the rail marks the wall with the puff's decal, where it marked with the weapon's and left nothing, and a puff without the flag still leaves the weapon's nothing.
//
// usage:  node scenarios/railforcedecal.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { parseDecalReport } from "./decalrules.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] Two rails that differ only in their puff: one forces its decal, the other only has one.
const rail = (name, puff) => `actor ${name} : Weapon
{
	Weapon.AmmoUse 0
	States
	{
	Ready:
		PISG A 1 A_WeaponReady
		Loop
	Deselect:
		PISG A 1 A_Lower
		Loop
	Select:
		PISG A 1 A_Raise
		Loop
	Fire:
		PISG A 20 A_RailAttack(0, 0, 0, "none", "none", 0, 0, "${puff}")
		Goto Ready
	}
}
`;
export const DECORATE = `actor ZXForcePuff : BulletPuff
{
	+ALWAYSPUFF
	+FORCEDECAL
	Decal BulletChip
}
actor ZXPlainPuff : BulletPuff
{
	+ALWAYSPUFF
	Decal BulletChip
}
${rail("ZXRailForce", "ZXForcePuff")}${rail("ZXRailPlain", "ZXPlainPuff")}`;

export function buildRailForceDecalPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }, { name: "decorate.txt", data: DECORATE }]);
}

// [rc4l] What is wrong with the live wall decals counted before, after the forced puff's rail and after the plain one's.
export function judgeRailDecals({ before, forced, plain }) {
  if ([before, forced, plain].some((n) => n === null || n === undefined)) return [`fua_decal_report gave no live count (${before}, ${forced}, ${plain})`];
  const faults = [];
  if (forced - before !== 1) faults.push(`the FORCEDECAL puff's rail left ${forced - before} wall decals, not 1`);
  if (plain - forced !== 0) faults.push(`the plain puff's rail left ${plain - forced} wall decals, where its weapon has none`);
  return faults;
}

const liveWalls = async (c) => parseDecalReport(await cap.exec(c, "fua_decal_report", { quietMs: 300 })).walls;

// [rc4l] One rail with the named weapon, stood square to the north wall at x.
async function fireRail(c, weapon, x) {
  await cap.exec(c, `give ${weapon}`);
  await cap.exec(c, `use ${weapon}`);
  await c.rpc("player.setpos", { x, y: 256, z: 0, angle: 90, pitch: 0 });
  await cap.waitTics(c, 35);
  await cap.exec(c, "+attack", { quietMs: 60 });
  await cap.waitTics(c, 4);
  await cap.exec(c, "-attack", { quietMs: 60 });
  await cap.waitTics(c, 35);
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const before = await liveWalls(c);
  await fireRail(c, "ZXRailForce", 256);
  const forced = await liveWalls(c);
  await fireRail(c, "ZXRailPlain", 640);
  const plain = await liveWalls(c);
  const faults = judgeRailDecals({ before, forced, plain });
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the FORCEDECAL puff marked the wall, the plain one left the weapon's nothing`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "railforcedecal", wad: buildRailForceDecalPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
