// [Doruk-Aksoy] Rooms of translated pillars, more of them than a 128-slot table holds but few in any one room, drawn with the full table and again with fua_dg_slotcap 160 in one launch: exit 0 when the small table recycles slots, stays bindless and draws every room exactly as the full one does.
//
// usage:  node scenarios/slotrecycle.mjs [port]
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { decodePNG, boxDiff } from "../src/pngcore.mjs";
import { moveTo, captureVulkan } from "../src/shot.mjs";
import * as cap from "../src/capture.mjs";

// [Doruk-Aksoy] 40 pillars a room, each its own class with its own translation, so each takes a slot of its own.
export const ROOMS = 5, PER_ROOM = 40, FIRST_EDNUM = 15200;
const ROOM_STEP = 2048;

// [Doruk-Aksoy] Pillar i maps every palette index onto 16 entries from i, a palette no other pillar has.
export function pillarDecorate() {
  let out = "";
  for (let i = 0; i < ROOMS * PER_ROOM; i++)
    out += `actor ZXSlotPillar${i} : TechPillar ${FIRST_EDNUM + i}\n{\n  Translation "0:255=${i}:${i + 15}"\n}\n`;
  return { name: "DECORATE", data: out };
}

// [Doruk-Aksoy] Sealed rooms side by side, each with its pillars in a grid north of where the player stands.
export function buildSlotRecycleWad() {
  const m = new UdmfMap();
  for (let r = 0; r < ROOMS; r++) {
    const x0 = r * ROOM_STEP;
    m.room(x0, 0, x0 + 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 },
      () => ({ side: { texturemiddle: "BROWN1" } }));
    for (let k = 0; k < PER_ROOM; k++)
      m.thing({ x: x0 + 160 + (k % 8) * 96, y: 400 + Math.floor(k / 8) * 110, angle: 270, type: FIRST_EDNUM + r * PER_ROOM + k });
  }
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad([pillarDecorate(), ...mapLumps("MAP01", m)]);
}

// [Doruk-Aksoy] Every room in turn, then back to the first and the middle, whose slots the small table has recycled by then.
const roomAt = (r) => ({ x: r * ROOM_STEP + 512, y: 160, z: 0, angle: 90, pitch: 0 });
export const VISITS = [0, 1, 2, 3, 4, 0, 2];

// [Doruk-Aksoy] The table line fua_dg_dynstats prints.
export const SLOT_LINE = /material slots: (\d+) of (\d+) in the table, (\d+) pinned, (\d+) recycled since load, bindless (on|off)/;
const PAST_FILL = /past the fill \((\d+) since load\)/;

// [Doruk-Aksoy] The verdict: the small table must have recycled, stayed bindless, put nothing past the fill and drawn every visit as the full table did.
export function judgeSlotRecycle(label, { stats, diffs }) {
  const m = SLOT_LINE.exec(stats), past = PAST_FILL.exec(stats);
  if (!m) return { line: `FAIL ${label}: fua_dg_dynstats printed no material slot line`, failed: 1 };
  const [, inTable, capacity, pinned, recycled, bindless] = m;
  const why = [];
  if (!(Number(recycled) > 0)) why.push("nothing was recycled");
  if (bindless !== "on") why.push("bindless turned off");
  if (past && Number(past[1]) > 0) why.push(`${past[1]} slots past the fill`);
  const off = diffs.filter((d) => !(d.diff === 0));
  for (const d of off) why.push(`visit ${d.visit} (room ${d.room}) ${d.diff === null ? "not captured" : `differs by ${d.diff.toFixed(4)}`}`);
  const facts = `${inTable} of ${capacity} slots, ${pinned} pinned, ${recycled} recycled`;
  return why.length ? { line: `FAIL ${label}: ${why.join("; ")} (${facts})`, failed: 1 }
    : { line: `PASS ${label}: ${diffs.length} visits drawn alike, ${facts}`, failed: 0 };
}

// [Doruk-Aksoy] One pass over the visits, each frame captured from where the player stands.
async function visitAll(c, tag) {
  const dir = path.join(os.tmpdir(), "fuactl-slotrecycle");
  fs.mkdirSync(dir, { recursive: true });
  const files = [];
  for (const [i, r] of VISITS.entries()) {
    await moveTo(c, roomAt(r));
    await cap.waitTics(c, 10);
    files.push(await captureVulkan(c, path.join(dir, `${tag}_${i}_room${r}.png`).split(path.sep).join("/")));
  }
  return files;
}

// [Doruk-Aksoy] Below the console's notice line, which a capture's own "wrote" message and a level's start print into.
const SCENE = [0, 0.03, 1, 1];

export async function judgeLaunch(c, inst, label) {
  const full = await visitAll(c, "full");
  await cap.exec(c, "fua_dg_slotcap 160");
  await cap.exec(c, "map MAP01", { quietMs: 400 });
  await cap.waitInLevel(c);
  const small = await visitAll(c, "small");
  const stats = await cap.exec(c, "fua_dg_dynstats", { quietMs: 400 });
  const diffs = VISITS.map((room, visit) => ({ visit, room,
    diff: full[visit] && small[visit] ? boxDiff(decodePNG(full[visit]), decodePNG(small[visit]), SCENE, 1) : null }));
  return judgeSlotRecycle(label, { stats, diffs });
}

runScenarioIfMain(import.meta.url, () => ({ name: "slotrecycle", wad: buildSlotRecycleWad(), maps: ["MAP01"],
  sides: [{ name: "vk", noGl: true, launchCvars: { show_messages: 0 } }], judgeLaunch }), compareEachLaunch);
