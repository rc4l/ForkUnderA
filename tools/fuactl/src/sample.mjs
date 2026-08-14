// Function-level hotspot attribution — "which functions are called a lot" — by sampling the RUNNING
// process externally. Zero source instrumentation, zero backport cost. The *in-engine* perf (frametime
// percentiles, sim/render split, counters, GPU timer queries) is plain C++ and identical on every
// platform; only THIS sampler is OS-specific, so it's a pluggable backend:
//   macOS   -> `sample`   (built-in, no install)
//   Linux   -> `perf record`/`perf report` (standard; may need perf_event_paranoid<=1 or CAP_PERFMON)
//   Windows -> no clean built-in CLI sampler -> Tracy (BSD, cross-platform) or Windows Perf Recorder
// All backends return the same shape: { available, functions: [{symbol, binary, samples}] } hottest first.
import { execFile } from "node:child_process";
import os from "node:os";
import path from "node:path";

const ENGINE_HINT = "forkundera"; // matches forkundera / forkundera.exe

// ---- Parsers (pure, unit-tested) ------------------------------------------

// macOS `sample` "Sort by top of stack" section.
export function parseTopOfStack(stdout, { top = 12, onlyBinary = null } = {}) {
  const lines = stdout.split("\n");
  let i = lines.findIndex((l) => /Sort by top of stack/.test(l));
  const out = [];
  if (i === -1) return out;
  for (i += 1; i < lines.length; i++) {
    const l = lines[i];
    if (!l.trim()) break;
    const m = l.match(/^\s+(.*?)\s+\(in\s+(.+?)\)\s+(\d+)\s*$/);
    if (!m) continue;
    const [, symbol, binary, samples] = m;
    if (onlyBinary && !binary.includes(onlyBinary)) continue;
    out.push({ symbol, binary, samples: Number(samples) });
  }
  return out.sort((a, b) => b.samples - a.samples).slice(0, top);
}

// Linux `perf report --stdio` rows: "  12.34%  comm  dso  [.] symbol".
export function parseLinuxPerf(stdout, { top = 12, onlyBinary = null } = {}) {
  const out = [];
  for (const l of stdout.split("\n")) {
    if (!l.trim() || l.trim().startsWith("#")) continue;
    const m = l.match(/^\s*([\d.]+)%\s+\S+\s+(\S+)\s+\[[^\]]*\]\s+(.+?)\s*$/);
    if (!m) continue;
    const [, pct, dso, symbol] = m;
    if (onlyBinary && !dso.includes(onlyBinary)) continue;
    out.push({ symbol, binary: dso, samples: Number(pct) }); // samples = percent for perf
  }
  return out.sort((a, b) => b.samples - a.samples).slice(0, top);
}

// ---- Backends --------------------------------------------------------------

function run(cmd, args, timeoutMs) {
  return new Promise((resolve) => {
    execFile(cmd, args, { maxBuffer: 64 * 1024 * 1024, timeout: timeoutMs }, (err, stdout, stderr) => {
      resolve({ err, stdout: stdout || "", stderr: stderr || "" });
    });
  });
}

async function sampleMac(pid, seconds, top, engineOnly) {
  const { err, stdout } = await run("sample", [String(pid), String(seconds), "-mayDie"], (seconds + 15) * 1000);
  if (!stdout) return { available: false, backend: "sample", error: (err && err.message) || "no output", functions: [] };
  return { available: true, backend: "sample", seconds, functions: parseTopOfStack(stdout, { top, onlyBinary: engineOnly ? ENGINE_HINT : null }) };
}

async function sampleLinux(pid, seconds, top, engineOnly) {
  const out = path.join(os.tmpdir(), `fuactl-perf-${pid}.data`);
  const rec = await run("perf", ["record", "-g", "-o", out, "-p", String(pid), "--", "sleep", String(seconds)], (seconds + 20) * 1000);
  if (rec.err && /not found|ENOENT/.test(rec.err.message)) return { available: false, backend: "perf", error: "perf not installed", functions: [] };
  if (/perf_event_paranoid|Permission|not permitted/i.test(rec.stderr)) {
    return { available: false, backend: "perf", error: "perf blocked (set kernel.perf_event_paranoid<=1 or grant CAP_PERFMON)", functions: [] };
  }
  const rep = await run("perf", ["report", "--stdio", "-i", out, "-g", "none", "--percent-limit", "0.3"], 30000);
  if (!rep.stdout) return { available: false, backend: "perf", error: "perf report produced no output", functions: [] };
  return { available: true, backend: "perf", seconds, functions: parseLinuxPerf(rep.stdout, { top, onlyBinary: engineOnly ? ENGINE_HINT : null }) };
}

// Sample `pid` for `seconds`, returning the hottest functions (engineOnly filters to the engine
// binary, the actionable "what in OUR code is hot"). Cross-platform via the pluggable backend.
export function sampleProcess(pid, { seconds = 2, top = 12, engineOnly = false } = {}) {
  const plat = os.platform();
  if (plat === "darwin") return sampleMac(pid, seconds, top, engineOnly);
  if (plat === "linux") return sampleLinux(pid, seconds, top, engineOnly);
  return Promise.resolve({
    available: false, backend: "none",
    error: "no built-in CLI sampler on Windows — use Tracy (BSD, cross-platform) or Windows Performance Recorder (wpr/WPA). The in-engine perf metrics (frametime/percentiles/sim-render split/counters) work here regardless.",
    functions: [],
  });
}
