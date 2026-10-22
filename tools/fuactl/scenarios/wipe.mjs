// [rc4l] The level change players see, Vulkan alone: nextmap melts the level into the intermission, and Use melts the intermission into MAP02, every wipe presented frame by frame (fua_wipe_trace) and the intermission, not the old map, on screen until the second one begins.
//
// usage:  node scenarios/wipe.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { scaled } from "../src/timeouts.mjs";

// [rc4l] Two rooms that look nothing alike, so each wipe's two ends are far apart.
function room(light, wall, floor) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: floor, textureceiling: "CEIL3_5", lightlevel: light },
    () => ({ side: { texturemiddle: wall } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return m;
}

export function buildWipeWad() {
  return wad([...mapLumps("MAP01", room(255, "STARTAN2", "FLAT14")), ...mapLumps("MAP02", room(96, "BRICK7", "FLOOR4_8"))]);
}

export const SIDES = [{ name: "vk" }];

// [rc4l] fua_wipe_report's answer: how many wipes began and how many the backend presented, then each one's frames.
export function parseWipeReport(text) {
  const head = /wipes started (\d+), presented (\d+)/.exec(String(text));
  if (!head) return null;
  const wipes = [];
  for (const line of String(text).split(/\r?\n/)) {
    const w = /^wipe (\d+): (\d+) frames, before to after (-?[\d.]+), before to the last wipe's after (-?[\d.]+)/.exec(line);
    if (w) { wipes.push({ frames: Number(w[2]), span: Number(w[3]), sinceLast: Number(w[4]), toBefore: [], toAfter: [] }); continue; }
    const f = /^\s+frame \d+: (-?[\d.]+) from before, (-?[\d.]+) from after/.exec(line);
    if (f && wipes.length) { wipes[wipes.length - 1].toBefore.push(Number(f[1])); wipes[wipes.length - 1].toAfter.push(Number(f[2])); }
  }
  return { started: Number(head[1]), presented: Number(head[2]), wipes };
}

// [rc4l] A wipe that went from one picture to the other: enough frames, the first near the picture before, the last near the one after, and frames between that are neither; null when it did, else what it did instead.
export function wipeFault(w, { minFrames = 8, near = 0.35, between = 0.15, minBetween = 3 } = {}) {
  if (w.frames < minFrames) return `${w.frames} frames presented, wanted ${minFrames} or more`;
  if (!(w.span > 5)) return `its two ends look alike (${w.span}), so it shows nothing`;
  if (!(w.toBefore[0] <= near * w.span)) return `its first frame is ${w.toBefore[0]} from the picture before it, of ${w.span}: it did not start there`;
  if (!(w.toAfter[w.toAfter.length - 1] <= near * w.span)) return `its last frame is ${w.toAfter[w.toAfter.length - 1]} from the picture after it, of ${w.span}: it did not end there`;
  const mid = w.toBefore.filter((d, i) => d >= between * w.span && w.toAfter[i] >= between * w.span).length;
  if (mid < minBetween) return `${mid} frames between the two pictures, wanted ${minBetween}: it cut rather than melted`;
  return null;
}

// [rc4l] The level change's verdict: every wipe begun was presented, each one went from picture to picture, and the second began from the intermission rather than the old map.
export function judgeWipes(label, report) {
  if (!report) return { line: `FAIL ${label}: no fua_wipe_report answer`, failed: 1 };
  const faults = [];
  if (report.started < 2) faults.push(`${report.started} wipes began, wanted the intermission's and MAP02's`);
  if (report.presented !== report.started) faults.push(`${report.started - report.presented} of ${report.started} wipes never reached the screen`);
  report.wipes.forEach((w, i) => { const f = wipeFault(w); if (f) faults.push(`wipe ${i + 1}: ${f}`); });
  const [first, second] = report.wipes;
  if (first && second && !(second.sinceLast <= 0.5 * first.span)) {
    faults.push(`before the second wipe the screen was ${second.sinceLast} from the intermission, of ${first.span}: the old map showed through`);
  }
  if (faults.length) return { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 };
  return { line: `PASS ${label}: ${report.wipes.map((w, i) => `wipe ${i + 1} ${w.frames} frames`).join(", ")}`, failed: 0 };
}

// [rc4l] Whether the instance is playing MAP02, which the intermission ends in.
async function inMap02(c) {
  const cam = await c.rpc("player.camera", {}).catch(() => ({}));
  const tic = await c.rpc("sim.tic").catch(() => ({}));
  return tic.inlevel && String(cam.map || "").toUpperCase() === "MAP02";
}

export async function judgeLaunch(c, inst, label) {
  await cap.exec(c, "fua_wipe_trace 1");
  await cap.waitTics(c, 35);
  await cap.exec(c, "nextmap", { quietMs: 400 });
  // [rc4l] The bridge answers again once the melt into the intermission has run; the stats then count up.
  await cap.sleep(scaled(2500));
  for (let press = 0; press < 8 && !(await inMap02(c)); press++) {
    await cap.exec(c, "+use", { quietMs: 60 });
    await cap.exec(c, "-use", { quietMs: 60 });
    await cap.sleep(scaled(700));
  }
  if (!(await inMap02(c))) return { line: `FAIL ${label}: Use never left the intermission for MAP02`, failed: 1 };
  await cap.waitTics(c, 35);
  return judgeWipes(label, parseWipeReport(await cap.exec(c, "fua_wipe_report", { quietMs: 400 })));
}

runScenarioIfMain(import.meta.url, () => ({ name: "wipe", wad: buildWipeWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch, needs: ["readback"] }),
  compareEachLaunch);
