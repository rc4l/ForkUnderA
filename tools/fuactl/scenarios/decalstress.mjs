// [Doruk-Aksoy] Turrets firing bullet marks at a room's pillars, windows and walls past cl_maxdecals while the player walks a fixed route, for what wall decals cost between builds: a churn window while they fire and a steady one after they stop.
//
// usage:  node scenarios/decalstress.mjs run <label> [port] [set]
//         node scenarios/decalstress.mjs ab <rounds> <port> <set> <set> ...
// A set is [engine][@k=v,k=v]: a binary (the staged one when left out) and launch cvars, as `@fua_decal_cache=0`.
// The turrets and the route run on the game's own clock, so every build sees the same marks from the same places.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { PCD, SCRIPT, acsScripts, callSpecial, gotoWord } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { isMain } from "../src/template.mjs";

export const CENTER = 1024;
export const TURRET = 30500;      // [Doruk-Aksoy] the turret's editor number
export const TURRET_TID = 10;
export const TURRETS = 16;
export const ROUTE_TICS = 700;    // [Doruk-Aksoy] one lap of the route, which then repeats
const ROUTE_RADIUS = 260;
// [Doruk-Aksoy] Tics of the level: the churn window starts once cl_maxdecals is full, the turrets go at `stop`, and the steady window holds the marks left.
export const TICS = { churn: [560, 1050], stop: 1050, steady: [1120, 1610] };
export const CVARS = { cl_maxdecals: 4096, vid_fps: 1, vid_maxfps: 0, vid_vsync: 0, i_pauseinbackground: 0, fua_render_in_background: 1 };

// [Doruk-Aksoy] Unseen and unhittable, so no turret stops another's fire, each puff marking the wall it hits with BulletChip.
export const DECORATE = `actor FuaDecalTurret ${TURRET}
{
\tRadius 4
\tHeight 8
\t+NOGRAVITY
\t+NOBLOCKMAP
\t+NOTELEPORT
\t+DONTSPLASH
\tDecal BulletChip
\tStates
\t{
\tSpawn:
\t\tTNT1 A 2 A_CustomBulletAttack(20, 8, 1, 0, "BulletPuff", 4096, CBAF_AIMFACING)
\t\tLoop
\t}
}
`;

// [Doruk-Aksoy] Where the player stands t tics into the route: a ring round the turrets, facing out and sweeping 50 degrees either way three times a lap.
export function routeAt(t) {
  const f = (t % ROUTE_TICS) / ROUTE_TICS, a = f * 2 * Math.PI;
  return { x: CENTER + ROUTE_RADIUS * Math.cos(a), y: CENTER + ROUTE_RADIUS * Math.sin(a), yaw: (a * 180) / Math.PI + 50 * Math.sin(a * 3) };
}

const fixed = (v) => Math.round(v * 65536);
const acsAngle = (deg) => Math.round((((deg % 360) + 360) % 360) / 360 * 65536) & 0xffff;

// [Doruk-Aksoy] The ENTER script that puts the player at each tic's place and angle in turn, then starts the lap again.
export function routeScript() {
  const code = [];
  for (let t = 0; t < ROUTE_TICS; t++) {
    const p = routeAt(t);
    code.push(PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, fixed(p.x), PCD.PUSHNUMBER, fixed(p.y), PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0,
      PCD.SETACTORPOSITION, PCD.DROP, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, acsAngle(p.yaw), PCD.SETACTORANGLE, PCD.DELAYDIRECT, 1);
  }
  return [...code, ...gotoWord(0)];
}

export function buildDecalStressWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 2 * CENTER, 2 * CENTER, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [Doruk-Aksoy] Eight blocks on a ring, closed pillars (one upper each side) between windows (a lower and an upper each side).
  for (let i = 0; i < 8; i++) {
    const a = ((i + 0.5) / 8) * 2 * Math.PI, x = Math.round(CENTER + 600 * Math.cos(a)), y = Math.round(CENTER + 600 * Math.sin(a));
    const sector = i % 2 === 0 ? { heightfloor: 0, heightceiling: 0 } : { heightfloor: 64, heightceiling: 192 };
    const side = () => ({ texturetop: "BROWN1", texturebottom: "BROWN1" });
    m.block(x - 64, y - 64, x + 64, y + 64, { ...sector, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, room, side, side);
  }
  const start = routeAt(0);
  m.thing({ x: Math.round(start.x), y: Math.round(start.y), angle: 0, type: 1 });
  for (let i = 0; i < TURRETS; i++) {
    const a = (i / TURRETS) * 360;
    m.thing({ x: Math.round(CENTER + 150 * Math.cos((a * Math.PI) / 180)), y: Math.round(CENTER + 150 * Math.sin((a * Math.PI) / 180)),
      height: 48, angle: Math.round(a), type: TURRET, id: TURRET_TID });
  }
  const stop = [PCD.DELAYDIRECT, TICS.stop, ...callSpecial(SPECIAL.Thing_Remove, [TURRET_TID]), PCD.TERMINATE];
  const behavior = acsScripts([{ code: routeScript(), type: SCRIPT.ENTER, number: 1 }, { code: stop, number: 2 }]);
  return wad([{ name: "DECORATE", data: DECORATE }, ...mapLumps("MAP01", m, { behavior })]);
}

// [Doruk-Aksoy] A set as `[engine][@k=v,k=v]`: the binary, undefined for the staged one, and its launch cvars.
export function parseSet(text) {
  const [engine, cvars = ""] = text.split("@");
  return { engine: engine || undefined, cvars: Object.fromEntries(cvars.split(",").filter(Boolean).map((kv) => kv.split("="))) };
}

// [Doruk-Aksoy] One capture's frames: their count, mean and 99th percentile frame time, and the mean of each zone or counter named.
export function summariseCapture(csvText, columns = ["scene/decals", "scene/decal_sides_kept", "scene/decal_sides_derived"]) {
  const [head, ...lines] = csvText.trim().split(/\r?\n/);
  const names = head.split(",");
  const rows = lines.map((l) => l.split(","));
  const col = (name) => { const i = names.indexOf(name); return rows.map((r) => (i >= 0 && r[i] !== "" ? Number(r[i]) : 0)); };
  const ms = col("frame/ms").sort((a, b) => a - b), n = ms.length;
  const mean = (xs) => (xs.length ? xs.reduce((s, v) => s + v, 0) / xs.length : 0);
  const out = { frames: n, frameMs: mean(ms), p99: n ? ms[Math.min(n - 1, Math.floor(n * 0.99))] : 0 };
  for (const c of columns) out[c] = names.includes(c) ? mean(col(c)) : null;
  return out;
}

// [Doruk-Aksoy] One launch of the map under a set, each window captured with fua_capture; the files it wrote, by window.
export async function runStress({ label, port, engine, cvars = {} }) {
  const { launchInstance, stopInstance } = await import("../src/launch.mjs");
  const { attach } = await import("../src/session.mjs");
  const { exec, sleep, waitInLevel } = await import("../src/capture.mjs");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "decalstress-"));
  const wadPath = path.join(dir, "decalstress.wad");
  fs.writeFileSync(wadPath, buildDecalStressWad());
  const inst = await launchInstance({ port, engine, map: "MAP01", focus: false, cvars: { ...CVARS, ...cvars }, extraArgs: ["-file", wadPath] });
  const c = await attach(inst);
  const tic = async () => { const r = await c.rpc("sim.tic"); return (r.result || r).gametic; };
  const files = {};
  try {
    await waitInLevel(c, 120000);
    for (const [name, [from, to]] of [["churn", TICS.churn], ["steady", TICS.steady]]) {
      while ((await tic()) < from) await sleep(5);
      await exec(c, "fua_capture stop", { quietMs: 300 });
      await exec(c, `fua_capture start ${label}-${name}`, { quietMs: 300 });
      while ((await tic()) < to) await sleep(5);
      const stopped = await exec(c, "fua_capture stop", { quietMs: 600, maxMs: 60000 });
      files[name] = /wrote (\S+\.csv)/.exec(stopped)?.[1];
    }
  } finally {
    c.close();
    await stopInstance(inst);
  }
  return files;
}

// [Doruk-Aksoy] The sets in turn, `rounds` times, then each window's captures side by side.
async function ab(rounds, port, sets) {
  const results = [];
  for (let r = 1; r <= rounds; r++)
    for (let i = 0; i < sets.length; i++) {
      const files = await runStress({ label: `decalstress${i + 1}-${r}`, port, ...parseSet(sets[i]) });
      results.push({ set: sets[i], round: r, files });
      console.log(`round ${r} set ${i + 1} (${sets[i]}): ${Object.values(files).join(" ")}`);
    }
  for (const window of ["churn", "steady"]) {
    console.log(`\n${window}: set, round, frames, frame ms, p99, decals ms, sides kept, sides derived`);
    for (const r of results) {
      const s = summariseCapture(fs.readFileSync(r.files[window], "utf8"));
      const v = (x, d) => (x === null ? "-" : x.toFixed(d));
      console.log(`  ${r.set.padEnd(40)} ${r.round} ${String(s.frames).padStart(6)} ${v(s.frameMs, 4)} ${v(s.p99, 2)} ${v(s["scene/decals"], 4)} ${v(s["scene/decal_sides_kept"], 1)} ${v(s["scene/decal_sides_derived"], 1)}`);
    }
  }
}

if (isMain(import.meta.url)) {
  const [mode, ...args] = process.argv.slice(2);
  if (mode === "run") {
    const [label, port, set = ""] = args;
    console.log(JSON.stringify(await runStress({ label, port: Number(port) || 7796, ...parseSet(set) })));
  } else if (mode === "ab") {
    const [rounds, port, ...sets] = args;
    await ab(Number(rounds) || 3, Number(port) || 7796, sets);
  } else {
    console.error("usage: node scenarios/decalstress.mjs run <label> [port] [set] | ab <rounds> <port> <set> <set> ...");
    process.exit(2);
  }
}
