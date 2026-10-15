// [rc4l] fuactl's views of features/telemetry: console commands an instance answers through the bridge, soaks the engine judges, and fua_stats readings compared against each other or a saved baseline.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import * as cap from "./capture.mjs";

// [rc4l] A console command on an instance, waited on as long as a telemetry command can take, its output returned.
export const execSlow = (c, text) => cap.exec(c, text, cap.SLOW_COMMAND);

// [rc4l] Each fuactl command that is one engine console command, built from its words and flags.
export const CONSOLE_VIEWS = {
  console: (words) => words.join(" "),
  stats: (words, flags) => ["fua_stats", flags.json ? "json" : flags.watch ? "watch" : "", words[0]].filter(Boolean).join(" "),
  hitches: (words) => `fua_hitches ${words[0] ?? 10}`,
  flight: (words) => ["fua_flightdump", words[0]].filter(Boolean).join(" "),
  memsnap: (words) => ["fua_memsnap", words[0]].filter(Boolean).join(" "),
  memdiff: (words) => ["fua_memdiff", ...words.slice(0, 2)].join(" "),
};

// [rc4l] The console text a view command runs, or null when cmd is not one.
export function consoleTextFor(cmd, words, flags = {}) {
  const view = CONSOLE_VIEWS[cmd];
  return view ? view(words, flags) : null;
}

// [rc4l] What a soak repeats, by name, as the console text a cycle runs on the map the soak began on and whether it starts a new level; tools/web/check.mjs repeats the same ones.
export const SOAK_ACTIONS = {
  reload: { text: (map) => `map ${map}`, newLevel: true },
};

// [rc4l] One cycle of a soak: a named action, else the console text as given.
export function soakStep(action, map) {
  const known = SOAK_ACTIONS[action];
  return known ? { text: known.text(map), newLevel: known.newLevel } : { text: action, newLevel: false };
}

// [rc4l] The first JSON value a console reply holds, which other output may surround.
export function parseConsoleJson(text) {
  const s = String(text);
  const start = s.search(/[[{]/);
  if (start < 0) throw new Error(`no JSON in: ${s.trim().slice(0, 120)}`);
  const open = s[start], close = open === "[" ? "]" : "}";
  let depth = 0, inString = false;
  for (let i = start; i < s.length; i++) {
    const ch = s[i];
    if (inString) { if (ch === "\\") i++; else if (ch === '"') inString = false; continue; }
    if (ch === '"') inString = true;
    else if (ch === open) depth++;
    else if (ch === close && --depth === 0) return JSON.parse(s.slice(start, i + 1));
  }
  throw new Error(`unterminated JSON in: ${s.trim().slice(0, 120)}`);
}

// [rc4l] Waits for the level a console command started: the level clock running backwards or the level left and entered again.
async function awaitNewLevel(c, before, { sleep, timeoutMs = 60000 }) {
  const deadline = Date.now() + timeoutMs;
  let left = false;
  while (Date.now() < deadline) {
    await sleep(250);
    const t = await c.rpc("sim.tic").catch(() => ({}));
    if (t.inlevel === false) left = true;
    if (t.inlevel && (left || (t.leveltime ?? 0) < before)) return;
  }
  throw new Error("soak: the level never came back");
}

// [rc4l] Repeats an action cycles times, marking memory after each once it settles, and returns the engine's verdict (fua_soak verdict json): pass, marks, judged, grew as { name, text } and the report it prints.
export async function runSoak(c, { action = "reload", cycles = 6, warmup = 1, settleMs = 4000, exec = execSlow, sleep = cap.sleep, log = () => {} }) {
  const cam = await c.rpc("player.camera");
  await exec(c, "fua_soak begin");
  for (let i = 0; i < cycles; i++) {
    const step = soakStep(action, cam.map);
    const before = (await c.rpc("sim.tic")).leveltime ?? 0;
    await exec(c, step.text);
    if (step.newLevel) await awaitNewLevel(c, before, { sleep });
    await sleep(settleMs);
    await exec(c, "fua_soak mark");
    log(`cycle ${i + 1}/${cycles}`);
  }
  return parseConsoleJson(await exec(c, `fua_soak verdict ${warmup} json`));
}

// [rc4l] The rows of two fua_stats json readings side by side, by name, with what a gate judges: p50 and p99 of each timing, regressed when over both share of the before and floor in its unit.
export function compareStats(before, after, { share = 0.15, floor = 0.1 } = {}) {
  const byName = new Map(before.map((r) => [r.name, r]));
  const rows = [];
  for (const b of after) {
    const a = byName.get(b.name);
    if (!a || !a.available || !b.available) continue;
    const judge = (key) => {
      const delta = b[key] - a[key];
      return { before: a[key], after: b[key], delta, regressed: b.unit === "ms" && delta > floor && delta > share * a[key] };
    };
    const p50 = judge("p50"), p99 = judge("p99");
    rows.push({ name: b.name, unit: b.unit, p50, p99, regressed: p50.regressed || p99.regressed });
  }
  return rows.sort((x, y) => Math.abs(y.p50.delta) - Math.abs(x.p50.delta));
}

const fmt = (v) => (Math.abs(v) >= 100 ? v.toFixed(0) : Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(2));

// [rc4l] A comparison as text, the regressions marked, its rows limited to those that changed.
export function formatCompare(rows, { limit = 40 } = {}) {
  const changed = rows.filter((r) => r.p50.delta !== 0 || r.p99.delta !== 0).slice(0, limit);
  if (!changed.length) return ["(no change)"];
  const width = Math.max(...changed.map((r) => r.name.length), 7);
  const lines = [`${"counter".padEnd(width)}  unit     p50 before -> after      p99 before -> after`];
  for (const r of changed)
    lines.push(`${r.name.padEnd(width)}  ${r.unit.padEnd(7)}  ${`${fmt(r.p50.before)} -> ${fmt(r.p50.after)}`.padEnd(22)}  ${fmt(r.p99.before)} -> ${fmt(r.p99.after)}${r.regressed ? "  REGRESSED" : ""}`);
  return lines;
}

// [rc4l] Where a named baseline lives: per machine, since timings from one machine judge nothing on another.
export function baselinePath(name, home = os.homedir()) {
  if (!/^[\w.-]+$/.test(name)) throw new Error(`baseline names are letters, digits, '.', '_' and '-': ${name}`);
  return path.join(home, ".forkundera", "baselines", `${name}.json`);
}

// [rc4l] A gate against a saved baseline: the first run, or --update, saves the readings; otherwise it compares and passes when nothing regressed.
export function perfGate(name, readings, { update = false, home, share, floor } = {}) {
  const file = baselinePath(name, home);
  if (update || !fs.existsSync(file)) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(readings, null, 1));
    return { saved: file, pass: true, rows: [] };
  }
  const rows = compareStats(JSON.parse(fs.readFileSync(file, "utf8")), readings, { share, floor });
  return { file, pass: !rows.some((r) => r.regressed), rows };
}
