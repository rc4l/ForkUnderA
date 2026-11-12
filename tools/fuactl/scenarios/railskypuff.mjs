// [rc4l] A rail's endpoint puff that went into the sky (zandronum@2e629a9b7b96): exit 0 when a puff on a horizon line goes as one on a sky floor does, where it stayed, and a plain wall's or a SKYEXPLODE puff's stays.
//
// usage:  node scenarios/railskypuff.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { railWeapon, fireRail } from "./railforcedecal.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] Puffs that stay until destroyed, so a count after the shot sees whether the rail took its puff away.
const stayPuff = (name, flags = "") => `actor ${name}
{
	+NOBLOCKMAP
	+NOGRAVITY
	+ALWAYSPUFF
	${flags}
	States
	{
	Spawn:
		PUFF A -1
		Stop
	}
}
`;
export const DECORATE = `${stayPuff("ZXStayPuff")}${stayPuff("ZXSkyStayPuff", "+SKYEXPLODE")}${railWeapon("ZXRailStay", "ZXStayPuff")}${railWeapon("ZXRailSkyStay", "ZXSkyStayPuff")}`;

// [rc4l] Each shot: the room's x span, where to stand, the weapon, its puff and how many of them must stay.
export const SHOTS = [
  { name: "a plain wall", x0: 0, weapon: "ZXRailStay", puff: "ZXStayPuff", stays: 1 },
  { name: "a horizon line", x0: 640, weapon: "ZXRailStay", puff: "ZXStayPuff", stays: 0 },
  { name: "a horizon line with SKYEXPLODE", x0: 640, dx: 128, weapon: "ZXRailSkyStay", puff: "ZXSkyStayPuff", stays: 1 },
  { name: "a sky floor", x0: 1280, weapon: "ZXRailStay", puff: "ZXStayPuff", stays: 0, pitch: 60 },
];

export function buildRailSkyPuffPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.room(640, 0, 1152, 512, ROOM, (edge) => (edge === "north" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } } : { side: { texturemiddle: "STARTAN2" } }));
  m.room(1280, 0, 1792, 512, { ...ROOM, texturefloor: "F_SKY1" }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }, { name: "decorate.txt", data: DECORATE }]);
}

// [rc4l] What is wrong with each shot's count of its puff class before and after it.
export function judgeSkyPuffs(results) {
  return results.filter((r) => r.after - r.before !== r.stays)
    .map((r) => `a rail into ${r.name} left ${r.after - r.before} of its puffs, not ${r.stays}`);
}

const countOf = async (c, cls) => ((await c.rpc("world.actors", {}))?.actors || []).filter((a) => a.c === cls).length;

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const results = [];
  for (const shot of SHOTS) {
    const before = await countOf(c, shot.puff);
    await fireRail(c, shot.weapon, shot.x0 + 256 + (shot.dx || 0), { pitch: shot.pitch || 0 });
    results.push({ ...shot, before, after: await countOf(c, shot.puff) });
  }
  const faults = judgeSkyPuffs(results);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the horizon line and the sky floor took their puffs, the plain wall and SKYEXPLODE kept theirs`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "railskypuff", wad: buildRailSkyPuffPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
