// [rc4l] fuactl's views of features/telemetry: console commands an instance answers through the bridge, soaks the engine judges, and fua_stats readings compared against each other or a saved baseline.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import * as cap from "./capture.mjs";
import { percentile } from "./stats.mjs";

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

// [rc4l] What a soak repeats, by name, as the console text a cycle runs on the map the soak began on; tools/web/check.mjs repeats the same ones.
export const SOAK_ACTIONS = {
  reload: (map) => `map ${map}`,
};

// [rc4l] The map console text starts (map, changemap, a wad_reload's map=, or fua_hostmap's, which joins the server it starts), "*" for a connect, whose map the server names, or null for text that starts none.
export function levelStarted(text) {
  const m = /^\s*(?:map|changemap|fua_hostmap)\s+(\S+)/i.exec(text) || /^\s*wad_reload\b.*\smap=(\S+)/i.exec(text);
  if (!m && /^\s*connect\s+\S/i.test(text)) return "*";
  return m ? m[1] : null;
}

// [rc4l] The size a step "check: resize WxH" asks the game window to take, as a person dragging its corner would, or null for any other step; a page soaked by tools/web takes the same step.
export function resizeAsked(text) {
  const m = /^check: resize (\d+)x(\d+)$/.exec(String(text).trim());
  return m ? [Number(m[1]), Number(m[2])] : null;
}

// [rc4l] One cycle of a soak as steps run in order: a named action, else console texts separated by "|", each with the map it starts (null for none), which the soak waits for.
export function soakSteps(action, map) {
  if (SOAK_ACTIONS[action] && !map) throw new Error(`soak: ${action} repeats the current map, and the instance is in none`);
  const texts = SOAK_ACTIONS[action] ? [SOAK_ACTIONS[action](map)] : String(action).split("|").map((t) => t.trim()).filter(Boolean);
  return texts.map((text) => ({ text, map: levelStarted(text) }));
}

// [rc4l] Whether a soak's cycles got slower: the later half's median against the earlier half's, past warmup, for the time a cycle's steps took and the frame time after them; each a line saying so.
export function soakSlower(cycles, warmup, { share = 0.25, floors = { stepSeconds: 0.25, frameMs: 0.1 } } = {}) {
  const judged = cycles.slice(Math.max(0, warmup));
  if (judged.length < 4) return [];
  const median = (v) => percentile(v, 50);
  const half = Math.floor(judged.length / 2);
  const out = [];
  for (const [key, floor] of Object.entries(floors)) {
    const before = median(judged.slice(0, half).map((c) => c[key])), after = median(judged.slice(half).map((c) => c[key]));
    if (after - before > floor && after > before * (1 + share)) out.push(`${key} went from ${before.toFixed(2)} to ${after.toFixed(2)}`);
  }
  return out;
}

// [rc4l] The cycles a soak ran, a line each: the time its steps took and the frame time after them.
export function soakCycleLines(cycles) {
  return cycles.map((c, i) => `cycle ${i + 1}: steps ${c.stepSeconds.toFixed(2)} s, frame p50 ${c.frameMs.toFixed(3)} ms`);
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

// [rc4l] A fua_allocsites report's stacks, each with its live count and bytes.
export function parseAllocReport(text) {
  const units = { B: 1, KB: 1024, MB: 1048576, GB: 1073741824 };
  const stacks = new Map();
  for (const line of String(text).split("\n")) {
    const m = /^\s+(\d+) \(([\d.]+) (B|KB|MB|GB)\)\s+(.+)$/.exec(line);
    if (m) stacks.set(m[4].trim(), { count: Number(m[1]), bytes: Number(m[2]) * units[m[3]] });
  }
  return stacks;
}

// [rc4l] The stacks whose live count grew from one report to the next, most first: a leak's stack grows with every cycle while the current level's stay put.
export function allocGrowth(before, after, limit = 12) {
  const grew = [];
  for (const [stack, now] of after) {
    const was = before.get(stack) ?? { count: 0, bytes: 0 };
    if (now.count > was.count) grew.push({ stack, grew: now.count - was.count, count: now.count, bytes: now.bytes });
  }
  return grew.sort((a, b) => b.grew - a.grew).slice(0, limit);
}

// [rc4l] The level clock once the level has run minTics, so a level started after it shows as the clock running backwards, or null outside a level (the title), after which any level is new.
async function playedLevelTime(c, { sleep, minTics = 35, timeoutMs = 60000 }) {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const t = await c.rpc("sim.tic").catch(() => ({}));
    if (!t.inlevel) return null;
    if ((t.leveltime ?? 0) >= minTics || Date.now() >= deadline) return t.leveltime ?? 0;
    await sleep(100);
  }
}

// [rc4l] Waits for the level a console command started: any level after none, the level clock running backwards, or the level left and entered again.
async function awaitNewLevel(c, before, { sleep, timeoutMs = 60000 }) {
  const deadline = Date.now() + timeoutMs;
  let left = false;
  while (Date.now() < deadline) {
    await sleep(100);
    const t = await c.rpc("sim.tic").catch(() => ({}));
    if (t.inlevel === false) left = true;
    if (t.inlevel && (before === null || left || (t.leveltime ?? 0) < before)) return;
  }
  throw new Error("soak: the level never came back");
}

// [rc4l] Repeats an action's steps on c, marking memory on judge (c, or a server c joins and leaves) after each cycle, and returns judge's fua_soak verdict with each cycle's times, what got slower and, with sites, the allocation stacks that grew.
export async function runSoak(c, { action = "reload", cycles = 6, warmup = 1, settleMs = 4000, ignore = [], judge = c, sites = false, resize = null, exec = execSlow, sleep = cap.sleep, now = Date.now, log = () => {} }) {
  const steps = soakSteps(action, (await c.rpc("player.camera").catch(() => ({}))).map);
  const ran = [];
  const record = sites && cycles - warmup >= 2;
  let firstReport = null;
  await exec(judge, "fua_soak begin");
  for (let i = 0; i < cycles; i++) {
    if (record && i === warmup) await exec(judge, "fua_allocsites start");
    const started = now();
    for (const step of steps) {
      const size = resizeAsked(step.text);
      if (size) {
        if (!resize) throw new Error(`soak: nothing here can size a window for "${step.text}"`);
        await resize(...size);
        await sleep(300);
        continue;
      }
      const before = step.map ? await playedLevelTime(c, { sleep }) : 0;
      await exec(c, step.text);
      if (step.map) await awaitNewLevel(c, before, { sleep });
    }
    const stepSeconds = (now() - started) / 1000;
    await sleep(settleMs);
    await exec(judge, "fua_soak mark");
    const frame = parseConsoleJson(await exec(judge, "fua_stats json frame/ms")).find((r) => r.name === "frame/ms");
    ran.push({ stepSeconds, frameMs: frame ? frame.p50 : 0 });
    log(soakCycleLines(ran).at(-1));
    if (record && i === warmup) firstReport = parseAllocReport(await exec(judge, "fua_allocsites report 5000"));
  }
  const verdict = parseConsoleJson(await exec(judge, ["fua_soak verdict", warmup, ...ignore.map((p) => `-${p}`), "json"].join(" ")));
  let grownSites = [];
  if (record) {
    grownSites = allocGrowth(firstReport, parseAllocReport(await exec(judge, "fua_allocsites report 5000")));
    await exec(judge, "fua_allocsites stop");
  }
  return { ...verdict, cycles: ran, slower: soakSlower(ran, warmup), sites: grownSites };
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

// [rc4l] Several readings of the same counters as one, each counter's p50 and p99 the median of its runs, so one noisy stretch of a fight cannot pass or fail a gate; a counter some run lacked keeps its first reading.
export function medianReadings(runs) {
  if (runs.length <= 1) return runs[0] || [];
  const median = (v) => [...v].sort((a, b) => a - b)[Math.floor(v.length / 2)];
  return runs[0].map((row) => {
    const same = runs.map((r) => r.find((x) => x.name === row.name)).filter((x) => x && x.available);
    if (!row.available || same.length < runs.length) return row;
    return { ...row, p50: median(same.map((x) => x.p50)), p99: median(same.map((x) => x.p99)) };
  });
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
