// [rc4l] fuactl present: what Windows actually put on screen, from Intel's PresentMon run as administrator, the compositor's own updates among it.
import fs from "node:fs";
import path from "node:path";
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { percentile } from "./stats.mjs";

const HERE = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "native", "presentmon");
export const PRESENTMON_VERSION = "2.6.0";
export const PRESENTMON_URL = `https://github.com/GameTechDev/PresentMon/releases/download/v${PRESENTMON_VERSION}/PresentMon-${PRESENTMON_VERSION}-x64.exe`;
// [rc4l] The compositor's process; its presents are its screen updates.
export const COMPOSITOR = "dwm.exe";

// [rc4l] Where the downloaded PresentMon lives, under build/ so git never sees it.
export function presentMonPath(dir = HERE) {
  return path.join(dir, "build", `PresentMon-${PRESENTMON_VERSION}-x64.exe`);
}

// [rc4l] PresentMon on disk, downloaded once and kept only when Intel's signature on it checks out.
export function ensurePresentMon({ exe = presentMonPath(), run = spawnSync, exists = fs.existsSync, log = () => {} } = {}) {
  if (!exists(exe)) {
    log(`downloading PresentMon ${PRESENTMON_VERSION}`);
    fs.mkdirSync(path.dirname(exe), { recursive: true });
    const r = run("curl", ["-sSL", "-o", exe, PRESENTMON_URL], { encoding: "utf8" });
    if (r.status !== 0) throw new Error(`PresentMon download failed: ${(r.stderr || "").trim()}`);
  }
  const sig = run("powershell", ["-NoProfile", "-Command", `$s = Get-AuthenticodeSignature '${exe}'; $s.Status.ToString() + '|' + $s.SignerCertificate.Subject`], { encoding: "utf8" });
  const [status, subject] = String(sig.stdout || "").trim().split("|");
  if (status !== "Valid" || !/O=Intel Corporation/.test(subject || "")) {
    fs.rmSync(exe, { force: true });
    throw new Error(`PresentMon at ${exe} is not validly signed by Intel (${status || "no signature"}), so it was removed`);
  }
  return exe;
}

// [rc4l] The PowerShell that runs PresentMon elevated for `seconds`, on `processName` or every process, waiting for it to finish.
export function adminScript(exe, { out, seconds, processName = null, session = "fuactl" }) {
  const args = [...(processName ? ["--process_name", processName] : []), "--output_file", out, "--timed", String(seconds),
    "--terminate_after_timed", "--qpc_time", "--session_name", session, "--stop_existing_session"];
  const quoted = args.map((a) => `'${String(a).replace(/'/g, "''")}'`).join(", ");
  return `# [rc4l] Written by fuactl present: PresentMon as administrator, which a UAC prompt asks for.\nStart-Process -FilePath '${exe}' -Verb RunAs -Wait -ArgumentList @(${quoted})\n`;
}

// [rc4l] Runs the admin script and resolves with its exit code once PresentMon has finished, keeping the event loop free for the engine's bridge meanwhile.
export function runAdminScript(ps1, { start = spawn } = {}) {
  return new Promise((resolve) => {
    const p = start("powershell", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ps1], { stdio: "ignore" });
    p.on("exit", (code) => resolve(code));
    p.on("error", () => resolve(-1));
  });
}

// [rc4l] The performance counter's ticks a second, which PresentMon's --qpc_time columns count in.
export function qpcFrequency(run = spawnSync) {
  const r = run("powershell", ["-NoProfile", "-Command", "[System.Diagnostics.Stopwatch]::Frequency"], { encoding: "utf8" });
  const hz = Number(String(r.stdout || "").trim());
  return hz > 0 ? hz : 1e7;
}

// [rc4l] PresentMon's CSV as objects by column, its byte-order mark dropped.
export function parsePresentMon(csv) {
  const lines = String(csv).replace(/^﻿/, "").split(/\r?\n/).filter(Boolean);
  const cols = (lines[0] || "").split(",");
  return lines.slice(1).map((l) => {
    const v = l.split(",");
    const row = {};
    cols.forEach((c, i) => { row[c] = v[i]; });
    return row;
  });
}

const num = (x) => { const n = Number(x); return x === undefined || x === "" || x === "NA" || Number.isNaN(n) ? null : n; };

// [rc4l] Per app and present mode, frames handed over and shown a second and the longest the screen held one, and the compositor's updates in each whole second; `qpcHz` turns TimeInQPC into seconds.
export function analyzePresentMon(rows, { qpcHz = 1e7 } = {}) {
  if (rows.length < 2) return null;
  const t = rows.map((r) => Number(r.TimeInQPC) / qpcHz);
  const t0 = Math.min(...t), seconds = Math.max(...t) - t0;
  const groups = new Map();
  rows.forEach((r) => {
    const key = `${r.Application}|${r.PresentMode}`;
    if (!groups.has(key)) groups.set(key, { app: r.Application, mode: r.PresentMode, handed: 0, shown: 0, maxHoldMs: 0 });
    const g = groups.get(key);
    g.handed++;
    if (num(r.MsUntilDisplayed) !== null) {
      g.shown++;
      g.maxHoldMs = Math.max(g.maxHoldMs, num(r.MsBetweenDisplayChange) || 0);
    }
  });
  const span = Math.max(seconds, 1e-9);
  const apps = [...groups.values()].map((g) => ({ ...g, handedPerS: g.handed / span, shownPerS: g.shown / span, shownPct: (100 * g.shown) / g.handed }))
    .sort((a, b) => b.handed - a.handed);
  const comp = rows.map((r, i) => [r, t[i]]).filter(([r]) => String(r.Application).toLowerCase() === COMPOSITOR).map(([, s]) => s);
  const perSecond = Array.from({ length: Math.floor(seconds) + 1 }, () => 0);
  for (const s of comp) perSecond[Math.floor(s - t0)]++;
  const gaps = comp.slice(1).map((s, i) => (s - comp[i]) * 1000);
  return {
    seconds, apps,
    compositor: comp.length ? { perSecond, median: percentile(perSecond, 50), gapMs: gaps.length ? { p50: percentile(gaps, 50), p99: percentile(gaps, 99), max: Math.max(...gaps) } : null } : null,
  };
}

// [rc4l] Frames a second the engine drew (fua_viewtrace rows, `view_at` on the same performance-counter clock in microseconds) against what PresentMon saw `app` hand over and Windows show, second by second.
export function alignSeconds(pmRows, traceRows, { app, qpcHz = 1e7 }) {
  const mine = pmRows.filter((r) => r.Application === app).map((r) => ({ s: Number(r.TimeInQPC) / qpcHz, shown: num(r.MsUntilDisplayed) !== null }));
  const drawn = traceRows.map((r) => r.view_at / 1e6);
  const all = [...mine.map((m) => m.s), ...drawn];
  if (!all.length) return [];
  const t0 = Math.min(...all), n = Math.floor(Math.max(...all) - t0) + 1;
  const out = Array.from({ length: n }, (_, i) => ({ second: i, drawn: 0, handed: 0, shown: 0 }));
  for (const s of drawn) out[Math.floor(s - t0)].drawn++;
  for (const m of mine) { const o = out[Math.floor(m.s - t0)]; o.handed++; if (m.shown) o.shown++; }
  return out;
}

// [rc4l] The analysis as lines a person reads.
export function formatPresentMon(a) {
  if (!a) return ["PresentMon saw fewer than two frames: was anything drawing (a game window draws only while it has focus)?"];
  const lines = [`${a.seconds.toFixed(1)} s captured`];
  for (const g of a.apps.slice(0, 8)) {
    lines.push(`  ${g.app.padEnd(24)} ${g.mode.padEnd(36)} handed ${Math.round(g.handedPerS)}/s, shown ${Math.round(g.shownPerS)}/s (${Math.round(g.shownPct)}%), longest held ${Math.round(g.maxHoldMs)} ms`);
  }
  if (a.compositor) {
    lines.push(`  the compositor updated the screen ${Math.round(a.compositor.median)} times a second (median; a healthy one runs near the refresh rate while anything moves): ${a.compositor.perSecond.join(" ")}`);
    if (a.compositor.gapMs) lines.push(`  between its updates: p50 ${a.compositor.gapMs.p50.toFixed(2)} p99 ${a.compositor.gapMs.p99.toFixed(1)} max ${a.compositor.gapMs.max.toFixed(1)} ms`);
  } else lines.push("  no compositor frames: capture every process (no --process) to see them");
  return lines;
}
