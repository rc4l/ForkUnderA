// [rc4l] What a frame costs at one view, from the engine's own reports in one command: the performance scope's step 0, which took four tools and a script.
import { summarise } from "./stats.mjs";
import * as cap from "./capture.mjs";

// [rc4l] The first number after `label` in `text`, or null.
function numberAfter(text, label) {
  const at = String(text).indexOf(label);
  if (at < 0) return null;
  const m = /-?\d+(?:\.\d+)?/.exec(String(text).slice(at + label.length));
  return m ? Number(m[0]) : null;
}

// [rc4l] The walk's clipping time, its slices and what it reached, from `fua_vis_report`.
export function parseVisReport(text) {
  const m = /clipping ([\d.]+) ms a frame over every walk, the player's in ([\d.]+) slices/.exec(String(text));
  const reach = /walks reach (\d+) subsectors and (\d+) segs/.exec(String(text));
  return {
    clipMs: m ? Number(m[1]) : null, slices: m ? Number(m[2]) : null,
    subsectors: reach ? Number(reach[1]) : null, segs: reach ? Number(reach[2]) : null,
  };
}

// [rc4l] The backend's world passes, frame phases, batches and sprites, from `fua_dg_dynstats`.
export function parseDynStats(text) {
  const t = String(text);
  const passes = /world passes last frame: (\d+), ([\d.]+) ms CPU:([^|\n]*)/.exec(t);
  const passList = passes ? [...passes[3].matchAll(/(\S+) ([\d.]+)/g)].map((p) => ({ name: p[1], ms: Number(p[2]) })) : [];
  const phases = {};
  const ph = /frame phases \(ms\):([^|\n]*)/.exec(t);
  if (ph) for (const p of ph[1].matchAll(/(\w+) ([\d.]+)/g)) phases[p[1]] = Number(p[2]);
  const batches = /batches: (\d+) drawn in (\d+) draw calls/.exec(t);
  return {
    passes: passes ? Number(passes[1]) : null, worldMs: passes ? Number(passes[2]) : null, passList, phases,
    batchesDrawn: batches ? Number(batches[1]) : null, worldDraws: batches ? Number(batches[2]) : null,
    sprites: numberAfter(t, "dynamic: "), dynBuildMs: numberAfter(t, "dyn build "),
  };
}

// [rc4l] The level's size and the backend's submit and GPU time a frame, from `fua_diligent_bench`.
export function parseBench(text) {
  const t = String(text);
  const size = /(\d+) verts \(\d+ tris\), (\d+) batches, (\d+) textures/.exec(t);
  const gpu = /GPU ([\d.]+) ms\/frame/.exec(t);
  return {
    verts: size ? Number(size[1]) : null, batches: size ? Number(size[2]) : null, textures: size ? Number(size[3]) : null,
    submitMs: numberAfter(t, "submit-only "), gpuMs: gpu ? Number(gpu[1]) : null,
  };
}

// [rc4l] The rounds' frame times as one median p50 and p99, beside the three reports.
export function costsSummary(rounds, vis, dyn, bench) {
  const p50 = summarise(rounds.map((r) => r.p50)), p99 = summarise(rounds.map((r) => r.p99));
  return { p50: p50 ? p50.median : null, p99: p99 ? p99.median : null, rounds: rounds.length, ...vis, ...dyn, ...bench };
}

// [rc4l] One line of the step-0 table and the world passes beneath it.
export function formatCosts(s) {
  const ms = (v) => (v == null ? "?" : `${v.toFixed(2)} ms`);
  const lines = [
    `frame p50 ${ms(s.p50)} p99 ${ms(s.p99)} | GPU ${ms(s.gpuMs)} | submit ${ms(s.submitMs)} | world passes ${s.passes ?? "?"} at ${ms(s.worldMs)} CPU | walk clip ${ms(s.clipMs)} | batches ${s.batches ?? "?"} (${s.batchesDrawn ?? "?"} drawn in ${s.worldDraws ?? "?"} calls) | sprites ${s.sprites ?? "?"} (build ${ms(s.dynBuildMs)}) | verts ${s.verts ?? "?"}`,
  ];
  if (s.passList.length) lines.push(`  passes: ${s.passList.map((p) => `${p.name} ${p.ms.toFixed(2)}`).join(", ")}`);
  const phases = Object.entries(s.phases);
  if (phases.length) lines.push(`  phases (ms): ${phases.map(([k, v]) => `${k} ${v.toFixed(2)}`).join(", ")}`);
  return lines.join("\n");
}

// [rc4l] A warm-up capture thrown away (a fresh launch is tic-locked until one has run), `rounds` measured, then the reports.
export async function measureCosts(c, { frames = 400, rounds = 3, bench = 120 } = {}) {
  await cap.capturePerf(c, { frames });
  const measured = [];
  for (let r = 0; r < rounds; r++) {
    const report = await cap.capturePerf(c, { frames });
    measured.push({ p50: report.total.p50_ms, p99: report.total.p99_ms });
  }
  const vis = parseVisReport(await cap.exec(c, "fua_vis_report"));
  const dyn = parseDynStats(await cap.exec(c, "fua_dg_dynstats"));
  const benchOut = await cap.exec(c, `fua_diligent_bench ${bench}`, { quietMs: 600, maxMs: 60000 });
  return costsSummary(measured, vis, dyn, parseBench(benchOut));
}
