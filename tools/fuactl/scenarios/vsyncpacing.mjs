// [Doruk-Aksoy] With vsync on, a one-frame stall must not leave the frames in a short/medium/long rhythm that each still shows for one refresh, the judder three swapchain images let in (fua_debug_swapchain_images 3 brings them back).
//
// usage:  node scenarios/vsyncpacing.mjs [port]
// Needs a real display: the rhythm comes from how the presentation engine hands images back, which no virtual screen does.
import fs from "node:fs";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { parseCaptureCsv } from "../src/recap.mjs";
import * as cap from "../src/capture.mjs";

// [Doruk-Aksoy] A frame under this share of the second's mean is a short one: in the rhythm they ran 1.1 ms against a 6.9 ms refresh.
export const SHORT_OF_MEAN = 0.4;
// [Doruk-Aksoy] A second with more short frames than this is in the rhythm, which made a third of them short; an even second has none.
export const RHYTHM_SHARE = 0.2;
// [Doruk-Aksoy] Each stall knocked three images into the rhythm about one time in four, so twenty miss it on a broken build well under one run in a hundred.
export const STALLS_US = [12000, 9000, 15000, 11000, 13000, 8000, 16000, 10000, 14000, 12000,
  12000, 9000, 15000, 11000, 13000, 8000, 16000, 10000, 14000, 12000];

// [Doruk-Aksoy] One room to stand in; what is drawn does not matter, only how the frames are paced.
export function buildVsyncPacingWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 256, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] A recorded second's frame times (fua_capture rows): how many, their mean, the share that were short and whether vsync was on throughout.
export function pacingOf(rows) {
  const ms = rows.map((r) => r["frame/ms"] || 0).filter((v) => v > 0);
  const mean = ms.length ? ms.reduce((a, b) => a + b, 0) / ms.length : 0;
  const short = ms.length ? ms.filter((v) => v < SHORT_OF_MEAN * mean).length / ms.length : 0;
  return { frames: ms.length, mean, short, vsync: rows.length > 0 && rows.every((r) => r["frame/vsync"] > 0) };
}

export function inRhythm(second) {
  return second.short > RHYTHM_SHARE;
}

const pct = (v) => `${Math.round(v * 100)}%`;

// [Doruk-Aksoy] Trials of { stall, first, after }, after measured only when first was in the rhythm: failing when the rhythm held for both seconds, since a blip that recovers is not the judder a player sees.
export function judgePacing(label, trials) {
  if (!trials.length) return { line: `FAIL ${label}: no stall was measured`, failed: 1 };
  const off = trials.find((t) => !t.first.vsync || (t.after && !t.after.vsync));
  if (off) return { line: `FAIL ${label}: vsync was off after the ${off.stall} us stall`, failed: 1 };
  const empty = trials.find((t) => t.first.frames === 0 || (t.after && t.after.frames === 0));
  if (empty) return { line: `FAIL ${label}: no frames were recorded after the ${empty.stall} us stall`, failed: 1 };
  const stuck = trials.filter((t) => inRhythm(t.first) && t.after && inRhythm(t.after));
  const refresh = trials[0].first.mean.toFixed(2);
  if (stuck.length) {
    const t = stuck[0];
    return { line: `FAIL ${label}: ${stuck.length} of ${trials.length} stalls left the frames in an uneven rhythm, `
      + `${pct(t.first.short)} then ${pct(t.after.short)} of frames short after the ${t.stall} us one (mean ${refresh} ms)`, failed: 1 };
  }
  const blips = trials.filter((t) => inRhythm(t.first)).length;
  return { line: `PASS ${label}: ${trials.length} stalls at a ${refresh} ms refresh, none left the frames in a rhythm`
    + (blips ? ` (${blips} recovered within a second)` : ""), failed: 0 };
}

// [Doruk-Aksoy] One second of frames recorded with fua_capture and read back from the CSV it wrote.
async function recordSecond(c) {
  await cap.exec(c, "fua_capture start vsyncpacing");
  await cap.sleep(1000);
  const stopped = await cap.exec(c, "fua_capture stop", { quietMs: 600, maxMs: 60000 });
  const csv = /wrote (\S+\.csv)/.exec(stopped);
  if (!csv) throw new Error(`fua_capture wrote no CSV: ${stopped.trim()}`);
  return pacingOf(parseCaptureCsv(fs.readFileSync(csv[1], "utf8")));
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 35);
  const trials = [];
  for (const stall of STALLS_US) {
    await cap.exec(c, `fua_debug_framestall_us ${stall}`);
    await cap.exec(c, "fua_debug_framestall_us 0");
    const first = await recordSecond(c);
    trials.push({ stall, first, after: inRhythm(first) ? await recordSecond(c) : null });
  }
  return judgePacing(label, trials);
}

runScenarioIfMain(import.meta.url, () => ({ name: "vsyncpacing", wad: buildVsyncPacingWad(), maps: ["MAP01"],
  sides: [{ name: "vk", cvars: { vid_vsync: 1 } }], judgeLaunch }), compareEachLaunch);
