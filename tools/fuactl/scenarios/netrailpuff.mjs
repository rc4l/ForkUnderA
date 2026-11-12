// [rc4l] A netgame's rail puffs (zandronum@7d12f80d3640): exit 0 when a client keeps no puff that went into the sky on its server, a horizon line's or a sky floor's, where the server told it of each before taking it away, and a client's own rail marks a wall with its FORCEDECAL puff's decal, where it never spawned the puff and marked nothing.
//
// usage:  node scenarios/netrailpuff.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { railWeapon } from "./railforcedecal.mjs";
import { parseDecalReport } from "./decalrules.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const RGF_SILENT = 1, RGF_EXPLICITANGLE = 4;
// [rc4l] Each turret's room on the server, the puff it fires each second and how its rail is aimed: level at the north wall, or down at the floor.
export const TURRETS = [
  { room: "horizon", x0: 640, puff: "ZXStayPuffH", ednum: 20311, spreadZ: 0, inSky: true },
  { room: "sky floor", x0: 1280, puff: "ZXStayPuffF", ednum: 20312, spreadZ: 60, inSky: true },
  { room: "plain wall", x0: 1920, puff: "ZXStayPuffW", ednum: 20313, spreadZ: 0, inSky: false },
];
const stayPuff = (name) => `actor ${name}\n{\n\t+NOBLOCKMAP\n\t+NOGRAVITY\n\t+ALWAYSPUFF\n\tStates\n\t{\n\tSpawn:\n\t\tPUFF A -1\n\t\tStop\n\t}\n}\n`;
const turret = (t) => `actor ZXTurret${t.puff.slice(-1)} ${t.ednum}\n{\n\tRadius 8\n\tHeight 16\n\tStates\n\t{\n\tSpawn:\n\t\tTNT1 A 35 A_CustomRailgun(0, 0, "none", "none", ${RGF_SILENT | RGF_EXPLICITANGLE}, 0, 0, "${t.puff}", 0, ${t.spreadZ})\n\t\tLoop\n\t}\n}\n`;
export const DECORATE = `${TURRETS.map((t) => stayPuff(t.puff) + turret(t)).join("")}actor ZXForcePuff : BulletPuff
{
	+ALWAYSPUFF
	+FORCEDECAL
	Decal BulletChip
}
${railWeapon("ZXRailForce", "ZXForcePuff")}`;

export function buildNetRailPuffPk3() {
  const m = new UdmfMap();
  const plain = () => ({ side: { texturemiddle: "STARTAN2" } });
  m.room(0, 0, 512, 512, ROOM, plain);
  // [rc4l] A start for each client, since the observer joining on the player's start telefragged it.
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  m.thing({ x: 128, y: 64, angle: 90, type: 2 });
  m.room(640, 0, 1152, 512, ROOM, (edge) => (edge === "north" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } } : plain()));
  // [rc4l] Sky above and below, so the turret's rail ends in the sky whichever way its pitch turns.
  m.room(1280, 0, 1792, 512, { ...ROOM, texturefloor: "F_SKY1", textureceiling: "F_SKY1" }, plain);
  m.room(1920, 0, 2432, 512, ROOM, plain);
  for (const t of TURRETS) m.thing({ x: t.x0 + 256, y: 128, angle: 90, type: t.ednum });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }, { name: "decorate.txt", data: DECORATE }]);
}

// [rc4l] What is wrong with the puffs each machine holds (counts by class) and the live wall decals the shooting client had before and after its rail.
export function judgeRailPuffs({ server, observer, decalsBefore, decalsAfter }) {
  const faults = [];
  for (const t of TURRETS) {
    const [s, o] = [server[t.puff] || 0, observer[t.puff] || 0];
    if (t.inSky && s !== 0) faults.push(`the server kept ${s} puffs from the ${t.room}`);
    if (t.inSky && o !== 0) faults.push(`the observer kept ${o} puffs the server took from the ${t.room}`);
    if (!t.inSky && !(s > 0 && o > 0)) faults.push(`the ${t.room}'s puffs are ${s} on the server and ${o} on the observer, where both must have some`);
  }
  if (decalsBefore === null || decalsAfter === null) faults.push("the shooting client's fua_decal_report gave no live count");
  else if (decalsAfter - decalsBefore !== 1) faults.push(`the client's own rail left ${decalsAfter - decalsBefore} wall decals, not its FORCEDECAL puff's 1`);
  return faults;
}

const countPuffs = async (c) => {
  const counts = {};
  for (const a of (await c.rpc("world.actors", { all: true, limit: 4096 }))?.actors || []) if (TURRETS.some((t) => t.puff === a.c)) counts[a.c] = (counts[a.c] || 0) + 1;
  return counts;
};
const liveWalls = async (c) => parseDecalReport(await cap.exec(c, "fua_decal_report", { quietMs: 300 })).walls;

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [player, observer] = set.clients;
  // [rc4l] The turrets fire once a second; three seconds is three puffs into each room.
  await cap.waitTics(sc, 105);
  const server = await countPuffs(sc), seen = await countPuffs(observer.c);
  await cap.exec(player.c, "give ZXRailForce");
  await cap.exec(player.c, "use ZXRailForce");
  await cap.waitTics(sc, 35);
  const decalsBefore = await liveWalls(player.c);
  await cap.exec(player.c, "+attack", { quietMs: 60 });
  await cap.waitTics(sc, 4);
  await cap.exec(player.c, "-attack", { quietMs: 60 });
  await cap.waitTics(sc, 35);
  const decalsAfter = await liveWalls(player.c);
  const faults = judgeRailPuffs({ server, observer: seen, decalsBefore, decalsAfter });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: no client kept a puff the sky took, and the client's own rail marked with its FORCEDECAL puff`], failed: faults.length ? 1 : 0 };
}

// [rc4l] Unlagged on, as a client draws its own rail, and its puff, only then.
export const CONFIGS = [{ name: "rails", players: 1, observers: 1, clientCvars: { cl_unlagged: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netrailpuff", wad: buildNetRailPuffPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
