// [rc4l] Frame cost under several console settings inside ONE running session, round by round, since a separate launch per setting drifted 30% and pointed queue #42 at the wrong cause.
import { summarise } from "./stats.mjs";
import * as cap from "./capture.mjs";

// [rc4l] Every setting once per round, rounds times over, so whatever the machine drifts by lands on all of them alike.
export function perfSchedule(settings, rounds) {
  const out = [];
  for (let r = 1; r <= rounds; r++) for (const setting of settings) out.push({ round: r, setting });
  return out;
}

// [rc4l] The names a setting assigns, one per `;`-separated "name value" command; a bare command assigns nothing.
export function settingNames(setting) {
  return String(setting).split(";").map((part) => part.trim().split(/\s+/)).filter((w) => w.length >= 2 && w[0]).map((w) => w[0]);
}

// [rc4l] A cvar's value from the console's `"name" is "value"` reply, or null when the name is not a cvar.
export function cvarValueOf(reply, name) {
  if (!/^\w+$/.test(name)) return null;
  const m = new RegExp(`"${name}" is "([^"]*)"`).exec(String(reply));
  return m ? m[1] : null;
}

// [rc4l] Each setting's console line with every other setting's cvars put back to where they started first, so a setting measures only itself and never one left over.
export function isolatedLines(settings, baselines) {
  const all = [...new Set(settings.flatMap(settingNames))].filter((n) => baselines[n] != null);
  return settings.map((s) => {
    const own = new Set(settingNames(s));
    return [...all.filter((n) => !own.has(n)).map((n) => `${n} ${baselines[n]}`), s].join("; ");
  });
}

// [rc4l] Each later setting against the first, paired within each round so the drift a round shared cancels: faster or slower only when every round agrees.
export function perfVerdicts(rows) {
  const rounds = new Map();
  const settings = [];
  for (const { round, setting, p50 } of rows) {
    if (!settings.includes(setting)) settings.push(setting);
    if (!rounds.has(round)) rounds.set(round, new Map());
    rounds.get(round).set(setting, p50);
  }
  const [base, ...others] = settings;
  return others.map((s) => {
    const deltas = [...rounds.values()].filter((r) => r.has(base) && r.has(s)).map((r) => +(r.get(s) - r.get(base)).toFixed(3));
    const d = summarise(deltas);
    const verdict = !d ? "insufficient data" : d.max < 0 ? "faster in every round" : d.min > 0 ? "slower in every round" : "no consistent difference";
    return { label: `${s} against ${base}`, verdict, deltas, median: d ? d.median : null };
  });
}

// [rc4l] Read where every named cvar starts, then for each setting put the others back, run it, let it settle and capture frames; restores the start and returns the rows and the verdicts.
export async function abPerf(c, settings, { rounds = 3, frames = 400, settleTics = 70, log = () => {} } = {}) {
  const baselines = {};
  for (const name of new Set(settings.flatMap(settingNames))) baselines[name] = cvarValueOf(await cap.exec(c, name), name);
  const lines = isolatedLines(settings, baselines);
  const rows = [];
  try {
    for (const { round, setting } of perfSchedule(settings, rounds)) {
      await cap.exec(c, lines[settings.indexOf(setting)]);
      await cap.waitTics(c, settleTics);
      const report = await cap.capturePerf(c, { frames });
      const row = { round, setting, p50: report.total.p50_ms, p99: report.total.p99_ms };
      rows.push(row);
      log(row);
    }
  } finally {
    const restore = Object.entries(baselines).filter(([, v]) => v != null).map(([n, v]) => `${n} ${v}`).join("; ");
    if (restore) await cap.exec(c, restore);
  }
  return { rows, verdicts: perfVerdicts(rows) };
}
