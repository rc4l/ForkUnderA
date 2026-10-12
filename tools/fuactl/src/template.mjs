// [rc4l] Template-map regression harness: GL against Vulkan on a generated wad, at fixed vantages, judged by box -- see the template-maps skill.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { BridgeClient } from "./client.mjs";
import * as cap from "./capture.mjs";
import * as shot from "./shot.mjs";
import * as ui from "./ui.mjs";
import { play, stopInstance } from "./play.mjs";
import { resolveEngine, engineProcessCount } from "./launch.mjs";
import { decodePNG, boxDiff } from "./pngcore.mjs";

// [rc4l] The repo's own Freedoom 2, found from this file so the harness runs from any checkout.
export const FREEDOOM2 = fileURLToPath(new URL("../../freedoom/freedoom2.wad", import.meta.url));

// [rc4l] The console lines a side runs once its level is up: each cvar, then its own commands (a re-bake, say).
export function sideCommands(side) {
  return [...Object.entries(side.cvars || {}).map(([k, v]) => `${k} ${v}`), ...(side.commands || [])];
}

// [rc4l] The console lines a side runs once it stands at a vantage, for a change the view's own walk has to see first.
export function vantageCommands(side) {
  return side.atVantage || [];
}

// [rc4l] The two launches a template compares by default; a scenario may name its own, each with the cvars it sets.
export const GL_VS_VULKAN = [{ name: "gl", gl: true }, { name: "vk", gl: false }];

// [rc4l] Whether a box's difference passes: at most its ceiling, and at least its floor when it must change.
export function boxWithin(d, { ceiling, floor }) {
  return (ceiling === undefined || d <= ceiling) && (floor === undefined || d >= floor);
}

// [rc4l] Mean |d| per box for one map, given each vantage's frame from both sides, as PASS/FAIL lines and a failure count.
export function judge(map, frames, boxes, size = [1280, 720], [a, b] = ["gl", "vk"]) {
  const lines = [];
  let failed = 0;
  for (const [vantage, f] of Object.entries(frames)) {
    const A = f[a], B = f[b];
    if (!A || !B) { lines.push(`FAIL ${map} ${vantage}: capture failed`); failed++; continue; }
    if (A.w !== size[0] || A.h !== size[1] || B.w !== A.w || B.h !== A.h) {
      lines.push(`FAIL ${map} ${vantage}: frame is ${A.w}x${A.h} / ${B.w}x${B.h}, boxes assume ${size[0]}x${size[1]}`);
      failed++;
      continue;
    }
    for (const [name, { box, ceiling, floor, block = 16, vantage: only }] of Object.entries(boxes)) {
      // [rc4l] A box naming a vantage is judged only there, for scenes whose vantages look at different parts of the frame.
      if (only && only !== vantage) continue;
      const d = boxDiff(A, B, box, block);
      const ok = boxWithin(d, { ceiling, floor });
      if (!ok) failed++;
      const limit = floor === undefined ? `ceiling ${ceiling}` : `floor ${floor}`;
      lines.push(`${ok ? "PASS" : "FAIL"} ${map} ${vantage.padEnd(8)} ${name.padEnd(8)} mean|d| ${d.toFixed(2).padStart(6)}  ${limit}`);
    }
  }
  return { lines, failed };
}

// [rc4l] One locked launch of the wad, handed to `body` and always torn down; a crash comes back as the error.
async function withEngine({ port, map, iwad, wadPath, gl, side = {}, size }, body) {
  let inst = null;
  const c = new BridgeClient();
  try {
    inst = await play({ port, map, iwad, file: wadPath, lock: true, gl, width: size[0], height: size[1] });
    await c.connect(inst.port, { token: inst.token });
    await c.waitHello();
    await cap.exec(c, "r_drawplayersprites 0");
    // [rc4l] GL keeps walking the scene, or nothing feeds the passes that still take from it.
    if (!gl) await cap.exec(c, "fua_dg_standalone 0");
    for (const line of sideCommands(side)) await cap.exec(c, line);
    await cap.waitTics(c, 10);
    return await body(c);
  } catch (e) {
    const died = inst && inst.log && fs.existsSync(`${inst.log}.crash`);
    throw new Error(died ? `engine crashed -- ${inst.log}.crash` : e.message);
  } finally {
    c.close();
    if (inst) await stopInstance(inst);
  }
}

// [rc4l] Which half of a Vulkan launch's GL/Vulkan pair a side keeps: Vulkan's, or with `frame: "gl"` GL's, drawn beside the backend's wall mesh.
export function pairFrame(side, pair) {
  return side.frame === "gl" ? pair.gl : pair.vk;
}

// [rc4l] Stand at a vantage and take one side's frame; GL alone comes from a Vulkan-off launch, the one reliable GL reference.
async function grab(c, exe, tag, at, side) {
  await c.rpc("sim.resume", {}).catch(() => {});
  await c.rpc("player.setpos", at);
  await cap.waitTics(c, 15);
  for (const line of vantageCommands(side)) await cap.exec(c, line);
  if (side.gl) return (await ui.screenshot(c, exe, `sweep/${tag}`)).path;
  return pairFrame(side, await shot.shotPair(c, tag, { engineBin: exe }));
}

// [rc4l] Build the wad, capture every map at every vantage from both sides, and judge; returns { lines, failed }.
export async function compareTemplate({ name, wad, maps, vantages, boxes, sides = GL_VS_VULKAN, iwad = FREEDOOM2, port = 7796,
  size = [1280, 720] }) {
  // [rc4l] GL reads its frame back out of the window, so another engine on top can make it garbage.
  const others = sides.some((sd) => sd.gl) ? engineProcessCount() : 0;
  if (others > 0) return { lines: [`${others} other engine window(s) open -- close them first`], failed: 1, refused: true };
  const exe = resolveEngine().split(path.sep).join("/");
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    const frames = {};
    try {
      for (const side of sides) {
        await withEngine({ port, map, iwad, wadPath, gl: !!side.gl, side, size }, async (c) => {
          for (const v of vantages) {
            const file = await grab(c, exe, `${name}_${map}_${v.name}_${side.name}`, v.at, side);
            (frames[v.name] ||= {})[side.name] = file ? decodePNG(file) : null;
          }
        });
      }
    } catch (e) {
      lines.push(`FAIL ${map}: ${e.message}`);
      failed++;
      continue;
    }
    const r = judge(map, frames, boxes[map], size, sides.map((sd) => sd.name));
    lines.push(...r.lines);
    failed += r.failed;
  }
  return { lines, failed };
}

// [rc4l] A scenario's options with cvars added to one named side, for running a regression test under the behaviour it guards against.
export function withSideCvars(opts, sideName, cvars) {
  const sides = opts.sides || GL_VS_VULKAN;
  if (!sides.some((s) => s.name === sideName)) throw new Error(`${opts.name} has no side ${sideName} (${sides.map((s) => s.name).join(", ")})`);
  return { ...opts, sides: sides.map((s) => (s.name === sideName ? { ...s, cvars: { ...(s.cvars || {}), ...cvars } } : s)) };
}

// [rc4l] A scenario's verdict as printed lines and an exit code: 0 all under ceiling, 1 failed, 3 refused.
export function scenarioOutcome(name, r) {
  const summary = r.failed ? `${r.failed} failed` : `all ${name} boxes within their limits`;
  return { lines: [...r.lines, summary], code: r.refused ? 3 : r.failed ? 1 : 0 };
}

// [rc4l] Whether the module at metaUrl is the file node was started with.
export function isMain(metaUrl, argv1 = process.argv[1]) {
  return Boolean(argv1) && metaUrl === pathToFileURL(argv1).href;
}

// [rc4l] Scenarios by module URL, kept when one is imported rather than run, so `fuactl probe` and `fuactl scenarios` can launch it.
const registered = new Map();
export function scenarioOptions(metaUrl) { const e = registered.get(metaUrl); return e ? e.options : null; }

// [rc4l] An imported scenario as one call that runs its comparison (its options passed through adjust) and returns its outcome, or null if the module registered none.
export function scenarioRun(metaUrl, adjust = (o) => o) {
  const e = registered.get(metaUrl);
  if (!e) return null;
  return async () => { const opts = adjust(e.options()); return scenarioOutcome(opts.name, await e.compare(opts)); };
}

// [rc4l] The template scenarios in dir (all, or those named), by file: the folder also holds measurement scripts that run as soon as they are imported.
export function templateScenarioFiles(dir, names = []) {
  return fs.readdirSync(dir).filter((f) => f.endsWith(".mjs")
    && (names.length === 0 || names.includes(f.slice(0, -4)))
    && fs.readFileSync(path.join(dir, f), "utf8").includes("runScenarioIfMain("));
}

// [rc4l] Run a scenario only when its own file is invoked (compareTemplate unless it names another), so importing its wad builder launches nothing; argv[2] is the port.
export function runScenarioIfMain(metaUrl, options, compare = compareTemplate) {
  if (!isMain(metaUrl)) { registered.set(metaUrl, { options, compare }); return; }
  const opts = options();
  // [rc4l] Named up front, since a scenario run directly takes the staged binary unless FUACTL_ENGINE says otherwise.
  console.log(`engine ${resolveEngine()}`);
  compare({ ...opts, port: Number(process.argv[2] || opts.port || 7796) }).then((r) => {
    const out = scenarioOutcome(opts.name, r);
    for (const l of out.lines) console.log(l);
    process.exit(out.code);
  }, (e) => { console.error(e); process.exit(2); });
}

// [rc4l] The map, vantage and side a probe uses: the named ones, else the first map, the first vantage and the first Vulkan side.
export function probeChoices({ maps, vantages, sides = GL_VS_VULKAN }, { map, vantage, side } = {}) {
  const v = vantage ? vantages.find((x) => x.name === vantage) : vantages[0];
  const sd = side ? sides.find((x) => x.name === side) : (sides.find((x) => !x.gl) || sides[0]);
  if (map && !maps.includes(map)) throw new Error(`no map ${map}: the scenario has ${maps.join(", ")}`);
  if (!v) throw new Error(`no vantage ${vantage}: the scenario has ${vantages.map((x) => x.name).join(", ")}`);
  if (!sd) throw new Error(`no side ${side}: the scenario has ${sides.map((x) => x.name).join(", ")}`);
  return { map: map || maps[0], vantage: v, side: sd };
}

// [rc4l] One launch of a scenario's map, standing at a vantage, running console commands and returning what each printed: the look before a box is drawn.
export async function probeScenario(opts, choice = {}, commands = []) {
  const { name, wad, iwad = FREEDOOM2, port = 7796, size = [1280, 720] } = opts;
  const { map, vantage, side } = probeChoices(opts, choice);
  const wadPath = path.join(os.tmpdir(), `fua-${name}-probe.wad`);
  fs.writeFileSync(wadPath, wad);
  return withEngine({ port, map, iwad, wadPath, gl: !!side.gl, side, size }, async (c) => {
    await c.rpc("player.setpos", vantage.at);
    await cap.waitTics(c, 15);
    const out = [];
    for (const command of commands) out.push({ command, output: await cap.exec(c, command, { quietMs: 400 }) });
    return out;
  });
}

// [rc4l] A counter the engine printed, held to its ceiling: the number pattern's first group captures in text, as a PASS/FAIL line and a failure count.
export function judgeCounter(map, text, { label, pattern, ceiling }) {
  const m = new RegExp(pattern).exec(String(text));
  if (!m) return { line: `FAIL ${map}: no ${label} in the engine's answer`, failed: 1 };
  const value = Number(m[1]);
  const ok = value <= ceiling;
  return { line: `${ok ? "PASS" : "FAIL"} ${map} ${label}: ${value} (ceiling ${ceiling})`, failed: ok ? 0 : 1 };
}

// [rc4l] One Vulkan launch per map, standing at each vantage with the sim running, then counter.command's number judged by judgeCounter: for a fault no paused frame can show, such as a one-frame flash the engine counts.
export async function compareCounter({ name, wad, maps, vantages, counter, iwad = FREEDOOM2, port = 7796, size = [1280, 720] }) {
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    try {
      const text = await withEngine({ port, map, iwad, wadPath, gl: false, size }, async (c) => {
        for (const v of vantages) {
          await c.rpc("sim.resume", {}).catch(() => {});
          await c.rpc("player.setpos", v.at);
          await cap.waitTics(c, 35);
        }
        return cap.exec(c, counter.command, { quietMs: 400 });
      });
      const r = judgeCounter(map, text, counter);
      lines.push(r.line);
      failed += r.failed;
    } catch (e) {
      lines.push(`FAIL ${map}: ${e.message}`);
      failed++;
    }
  }
  return { lines, failed };
}

// [rc4l] One Vulkan launch per map, each vantage captured twice intervalMs apart, judged before against after: for what must move (a warp) or must not.
// [rc4l] GL against Vulkan from ONE frame of one Vulkan launch, for what moves with time (a scroller, an animation), where two launches never agree on the moment.
export async function compareSameFrame({ name, wad, maps, vantages, boxes, iwad = FREEDOOM2, port = 7796, size = [1280, 720], settleTics = 70 }) {
  const others = engineProcessCount();
  if (others > 0) return { lines: [`${others} other engine window(s) open -- close them first`], failed: 1, refused: true };
  const exe = resolveEngine().split(path.sep).join("/");
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    const frames = {};
    try {
      await withEngine({ port, map, iwad, wadPath, gl: false, size }, async (c) => {
        await cap.waitTics(c, settleTics);
        for (const v of vantages) {
          await c.rpc("sim.resume", {}).catch(() => {});
          await c.rpc("player.setpos", v.at);
          await cap.waitTics(c, 15);
          const pair = await shot.shotPair(c, `${name}_${map}_${v.name}`, { engineBin: exe, sameFrame: true });
          frames[v.name] = { gl: pair.gl ? decodePNG(pair.gl) : null, vk: pair.vk ? decodePNG(pair.vk) : null };
        }
      });
    } catch (e) {
      lines.push(`FAIL ${map}: ${e.message}`);
      failed++;
      continue;
    }
    const r = judge(map, frames, boxes[map], size);
    lines.push(...r.lines);
    failed += r.failed;
  }
  return { lines, failed };
}

export async function compareOverTime({ name, wad, maps, vantages, boxes, intervalMs = 600, iwad = FREEDOOM2, port = 7796,
  size = [1280, 720] }) {
  const exe = resolveEngine().split(path.sep).join("/");
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    const frames = {};
    try {
      await withEngine({ port, map, iwad, wadPath, gl: false, size }, async (c) => {
        for (const v of vantages) {
          const { vk: [before, after] } = await shot.captureOverTime(c, { at: v.at, engineBin: exe,
            tag: `${name}_${map}_${v.name}`, intervalMs });
          frames[v.name] = { before: before ? decodePNG(before) : null, after: after ? decodePNG(after) : null };
        }
      });
    } catch (e) {
      lines.push(`FAIL ${map}: ${e.message}`);
      failed++;
      continue;
    }
    const r = judge(map, frames, boxes[map], size, ["before", "after"]);
    lines.push(...r.lines);
    failed += r.failed;
  }
  return { lines, failed };
}
