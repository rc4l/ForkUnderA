// [rc4l] Every tic made to cost 40 ms with tics on the game thread, as a Complex Doom fight's do: exit 0 when frames keep coming and the game slows, rather than a frame freezing for each owed tic or input running a second ahead (queue #70).
//
// usage:  node scenarios/ticoverload.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { medianReadings, parseConsoleJson } from "../src/telemetry.mjs";

// [rc4l] What each tic's playsim costs, and the tics a second that leaves room for.
export const TIC_US = 40000;
const SECONDS = 4;

export function buildTicOverloadWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 512, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] A frame that waited on an owed tic took at least that tic, so frames are judged against three quarters of one, which a window paced at 60 Hz in the background stays under.
export const FRAME_CEILING_MS = 0.75 * TIC_US / 1000;

// [rc4l] The verdict: frames under one heavy tic, the input made ahead held to kThreadedTicBacklog, and the game running at the rate its tics allow.
export function judgeTicOverload(label, { took, frameP99, backlogMax, ticsPerSecond }) {
  if (!took) return { line: `FAIL ${label}: fua_debug_playsim_us did not take, so this binary cannot slow its tics`, failed: 1 };
  const allowed = 1e6 / TIC_US;
  const faults = [];
  if (!(frameP99 < FRAME_CEILING_MS)) faults.push(`frames p99 ${frameP99.toFixed(1)} ms, so frames waited on owed tics`);
  if (!(backlogMax <= 3)) faults.push(`${backlogMax} tics owed at once, input running ahead of the game`);
  if (!(ticsPerSecond > allowed * 0.7 && ticsPerSecond < allowed * 1.15)) faults.push(`${ticsPerSecond.toFixed(1)} tics a second where ${TIC_US / 1000} ms tics allow ${allowed}`);
  const read = `frames p99 ${frameP99.toFixed(2)} ms, backlog max ${backlogMax}, ${ticsPerSecond.toFixed(1)} tics/s`;
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")} (${read})`, failed: 1 } : { line: `PASS ${label}: ${read}`, failed: 0 };
}

// [rc4l] One counter's reading from fua_stats json, NaN when the binary has none.
export function statOf(rows, name, field) {
  const row = rows.find((r) => r.name === name);
  return row ? Number(row[field]) : NaN;
}

export async function judgeLaunch(c, inst, label) {
  await cap.exec(c, `fua_debug_playsim_us ${TIC_US}`);
  const took = cap.echoedCvar(await cap.exec(c, "fua_debug_playsim_us"), "fua_debug_playsim_us") === String(TIC_US);
  // [rc4l] A second for the backlog to settle where it will, then the tics counted over a few more.
  await cap.sleep(1000);
  const start = cap.ticOf(await c.rpc("sim.tic"));
  const began = Date.now();
  await cap.sleep(SECONDS * 1000);
  const ticsPerSecond = (cap.ticOf(await c.rpc("sim.tic")) - start) / ((Date.now() - began) / 1000);
  // [rc4l] Only the two groups judged, since the whole table is printed over several slow frames, read three times a second apart and the frames judged on their median p99.
  const runs = [];
  for (let i = 0; i < 3; i++) {
    if (i > 0) await cap.sleep(1000);
    runs.push([...parseConsoleJson(await cap.exec(c, "fua_stats json frame/", cap.SLOW_COMMAND)), ...parseConsoleJson(await cap.exec(c, "fua_stats json handoff/", cap.SLOW_COMMAND))]);
  }
  await cap.exec(c, "fua_debug_playsim_us 0");
  const backlogMax = Math.max(...runs.map((rows) => statOf(rows, "handoff/backlog_tics", "max")));
  return judgeTicOverload(label, { took, frameP99: statOf(medianReadings(runs), "frame/ms", "p99"), backlogMax, ticsPerSecond });
}

// [rc4l] Tics on the game thread, where catch-up no longer runs inline; --side vk --cvar fua_gamethread=0 shows the frames freezing.
runScenarioIfMain(import.meta.url, () => ({ name: "ticoverload", wad: buildTicOverloadWad(), maps: ["MAP01"], sides: [{ name: "vk", cvars: { fua_gamethread: 1 } }], judgeLaunch }), compareEachLaunch);
