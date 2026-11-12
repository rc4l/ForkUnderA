// [rc4l] Rails a client did not predict mark where they stopped (zandronum@a8d26c5591fa): exit 0 when an observer holds wall decals from a server-side turret's rail with its own decal, and floor marks from another's whose FORCEDECAL puff alone has one, where it marked neither.
//
// usage:  node scenarios/netraildecal.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { parseDecalReport } from "./decalrules.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const RGF_SILENT = 1, RGF_EXPLICITANGLE = 4;
// [rc4l] Each turret's room, how its rail is aimed (level at the north wall, or down at the floor), and whose decal marks.
export const TURRETS = [
  { name: "ZXWallTurret", ednum: 20321, x0: 640, spreadZ: 0, puff: "BulletPuff", decal: "BulletChip", marks: "walls" },
  { name: "ZXFloorTurret", ednum: 20322, x0: 1280, spreadZ: 60, puff: "ZXDecalPuff", decal: null, marks: "flats" },
];
const turret = (t) => `actor ${t.name} ${t.ednum}\n{\n\tRadius 8\n\tHeight 16\n${t.decal ? `\tDecal ${t.decal}\n` : ""}\tStates\n\t{\n\tSpawn:\n\t\tTNT1 A 35 A_CustomRailgun(0, 0, "none", "none", ${RGF_SILENT | RGF_EXPLICITANGLE}, 0, 0, "${t.puff}", 0, ${t.spreadZ})\n\t\tLoop\n\t}\n}\n`;
// [rc4l] The floor turret has no decal, so only its puff's can mark.
export const DECORATE = `actor ZXDecalPuff : BulletPuff\n{\n\t+ALWAYSPUFF\n\t+FORCEDECAL\n\tDecal BulletChip\n}\n${TURRETS.map(turret).join("")}`;

export function buildNetRailDecalPk3() {
  const m = new UdmfMap();
  const plain = () => ({ side: { texturemiddle: "STARTAN2" } });
  m.room(0, 0, 512, 512, ROOM, plain);
  // [rc4l] A start for each client, so the observer does not telefrag the player.
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  m.thing({ x: 128, y: 64, angle: 90, type: 2 });
  for (const t of TURRETS) {
    m.room(t.x0, 0, t.x0 + 512, 512, ROOM, plain);
    m.thing({ x: t.x0 + 256, y: 128, angle: 90, type: t.ednum });
  }
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }, { name: "decorate.txt", data: DECORATE }]);
}

// [rc4l] What is wrong with the observer's live marks: some on walls from the wall turret, some on floors from the floor turret's puff.
export function judgeMarks(report) {
  if (!report || report.walls === null) return ["the observer's fua_decal_report gave no live count"];
  return TURRETS.flatMap((t) => (report[t.marks] > 0 ? [] : [`the observer holds no ${t.marks === "walls" ? "wall decals" : "floor marks"} from ${t.name}'s rails`]));
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const observer = set.clients.find((c) => c.role === "observer") || set.clients.at(-1);
  // [rc4l] The turrets fire once a second; three seconds is three rails each.
  await cap.waitTics(sc, 105);
  const faults = judgeMarks(parseDecalReport(await cap.exec(observer.c, "fua_decal_report", { quietMs: 300 })));
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the observer marked the wall turret's rails with its decal and the floor turret's with its puff's`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "rails", players: 1, observers: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netraildecal", wad: buildNetRailDecalPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
