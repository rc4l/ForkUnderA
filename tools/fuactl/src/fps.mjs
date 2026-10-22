// [rc4l] fuactl fps: one spot's frame rate under each renderer mode, each mode a locked launch of its own measured the same way.
import path from "node:path";
import * as cap from "./capture.mjs";
import * as ui from "./ui.mjs";
import { withLockedInstance } from "./play.mjs";
import { launchRenderer } from "./template.mjs";
import { percentile, refreshBoundHz, refreshNote } from "./stats.mjs";
import { sampleProcess } from "./sample.mjs";

// [rc4l] Each mode's launch: GL alone, the backend beside GL in one process, and the backend alone as players run it.
export const FPS_MODES = { gl: { gl: true }, beside: { glBeside: true }, vulkan: {} };

// [rc4l] Old names a --modes flag may still use.
const FPS_ALIASES = { embed: "beside" };

// [rc4l] The modes a --modes flag names, in its order and under their own names, or every mode when it names none.
export function fpsModes(flag) {
  const modes = flag && flag !== true ? String(flag).split(",").map((s) => s.trim()).filter(Boolean).map((m) => FPS_ALIASES[m] || m) : Object.keys(FPS_MODES);
  const bad = modes.filter((m) => !(m in FPS_MODES));
  if (bad.length) throw new Error(`unknown fps mode ${bad.join(", ")} (modes: ${Object.keys(FPS_MODES).join(", ")})`);
  return modes;
}

// [rc4l] One mode's row: the median of its rounds' p50 and p99, the frame rate that p50 is, and the refresh it sits on, if any.
export function fpsRow(mode, renderer, reports) {
  const p50s = reports.map((r) => r.total.p50_ms);
  const p50 = percentile(p50s, 50);
  return {
    mode, renderer,
    p50_ms: p50, p99_ms: percentile(reports.map((r) => r.total.p99_ms), 50),
    fps: Math.round(1000 / p50), rounds: p50s, boundHz: refreshBoundHz(p50),
  };
}

// [rc4l] A row as the line fps prints for it.
export function formatFpsRow(row) {
  const note = refreshNote(row.p50_ms);
  const shot = row.shot === undefined ? "" : `  shot ${row.shot || "FAILED"}`;
  return `${row.mode.padEnd(7)}${String(row.renderer || "?").padEnd(8)}p50 ${row.p50_ms.toFixed(3)} ms  ~${row.fps} fps  `
    + `p99 ${row.p99_ms.toFixed(3)} ms  (rounds ${row.rounds.map((v) => v.toFixed(3)).join(", ")})${note ? `  ${note}` : ""}${shot}`;
}

// [rc4l] Where a mode's shot lands and the command that takes it: GL's buffer for GL alone, else the backend's swapchain, as GL's buffer beside the backend is a stale frame beneath its window.
export function fpsShot(mode, engine, label) {
  const name = `sweep/fps_${label}_${mode}`;
  const file = path.join(path.dirname(engine), `${name}.png`).replace(/\\/g, "/");
  return { file, command: mode === "gl" ? `screenshot ${name}` : `fua_diligent_shot "${file}"` };
}

// [rc4l] Launch, warm, measure and tear down each mode in turn on one port, so no two engines ever run at once.
export async function runFps(opts, deps = {}) {
  const { withInstance = withLockedInstance, exec = cap.exec, capture = cap.capturePerf,
    shoot = ui.commandShot, sample = sampleProcess, log = () => {} } = deps;
  const rows = [];
  for (const mode of opts.modes) {
    rows.push(await withInstance({ ...opts.launch, ...FPS_MODES[mode] }, async (c, inst) => {
      await exec(c, `vid_vsync ${opts.vsync ? 1 : 0}`);
      const renderer = launchRenderer(await exec(c, "dumprenderer"));
      // [rc4l] Thrown away: a fresh launch renders once a tic until its first capture has run.
      await capture(c, { frames: opts.frames, warmup: opts.warmup });
      const reports = [];
      for (let r = 0; r < opts.rounds; r++) reports.push(await capture(c, { frames: opts.frames, warmup: 60 }));
      const row = fpsRow(mode, renderer, reports);
      if (opts.shot) {
        const { file, command } = fpsShot(mode, inst.engine, opts.label);
        row.shot = await shoot(c, command, file).then((s) => s.path, () => null);
      }
      if (opts.sample) row.sample = (await sample(inst.pid, { seconds: opts.sample, top: 8, conn: c })).functions;
      log(formatFpsRow(row));
      return row;
    }));
  }
  return rows;
}
