// [rc4l] Template-map regression harness: GL against Vulkan on a generated wad, at fixed vantages, judged by box -- see the template-maps skill.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import * as cap from "./capture.mjs";
import * as shot from "./shot.mjs";
import * as ui from "./ui.mjs";
import { withLockedInstance, launchMode, describeMode } from "./play.mjs";
import { resolveEngine, engineProcessCount, exited, slashed, launchInstance, stopInstance } from "./launch.mjs";
import { attach } from "./session.mjs";
import { scaled } from "./timeouts.mjs";
import { decodePNG, boxDiff } from "./pngcore.mjs";
import { runSoak } from "./telemetry.mjs";
import { launchNetplay, closeNetplay, seamViolations } from "./netplay.mjs";

// [rc4l] The repo's own Freedoom 2, found from this file so the harness runs from any checkout.
export const FREEDOOM2 = fileURLToPath(new URL("../../freedoom/freedoom2.wad", import.meta.url));
export const FREEDOOM1 = path.join(path.dirname(FREEDOOM2), "freedoom1.wad");

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

// [rc4l] Vulkan with the scene pinned at full scale against at half (features/dynres), for a pass that must land at half scale where it does at full.
export const FULL_VS_HALF = [{ name: "full", cvars: { fua_dg_dynres_force: 1 } }, { name: "half", cvars: { fua_dg_dynres_force: 0.5 } }];

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
    for (const [name, { box, ceiling, floor, block = 16, vantage: only, pair }] of Object.entries(boxes)) {
      // [rc4l] A box naming a vantage is judged only there, for scenes whose vantages look at different parts of the frame.
      if (only && only !== vantage) continue;
      // [rc4l] A box naming a pair of captures (compareSequence) is judged between those two.
      const [P, Q] = pair ? [f[pair[0]], f[pair[1]]] : [A, B];
      if (!P || !Q) { lines.push(`FAIL ${map} ${vantage} ${name}: no capture ${pair.join(" or ")}`); failed++; continue; }
      const d = boxDiff(P, Q, box, block);
      const ok = boxWithin(d, { ceiling, floor });
      if (!ok) failed++;
      const limit = floor === undefined ? `ceiling ${ceiling}` : `floor ${floor}`;
      lines.push(`${ok ? "PASS" : "FAIL"} ${map} ${vantage.padEnd(8)} ${name.padEnd(8)} mean|d| ${d.toFixed(2).padStart(6)}  ${limit}`);
    }
  }
  return { lines, failed };
}

export { launchRenderer, launchBackendApi } from "./capture.mjs";

// [rc4l] One locked launch of the wad with the side's settings applied, handed to `body` and always torn down; its launchCvars go on the command line, for what must hold from the level's upload, where its cvars are set once the level is up.
// [rc4l] Whether a side runs the backend beside GL in one process: one judged by GL's frame beside the backend's wall mesh (frame "gl"), by both renderers from one frame (glDraws), or one that asks (glBeside); every other Vulkan side runs as players do, without GL.
export function sideBesideGl(side = {}) {
  return !side.gl && !side.webgpu && (side.frame === "gl" || !!side.glDraws || !!side.glBeside);
}

function withEngine({ port, map, iwad, wadPath, gl, side = {}, size }, body) {
  const glBeside = sideBesideGl(side);
  const want = launchMode({ gl, glBeside, webgpu: !!side.webgpu });
  return withLockedInstance({ port, map, iwad, file: wadPath, gl, glBeside, webgpu: !!side.webgpu, extra: side.launchCvars, width: size[0], height: size[1] }, async (c, inst) => {
    // [rc4l] A side really runs the renderers it asked for: a binary that ignored -vulkan would pass as GL beside the backend, and a WebGPU device that failed falls back to Vulkan.
    const got = cap.launchModeOf(await cap.exec(c, cap.RENDERER_PROBE, { quietMs: 300 }));
    if (got !== want) throw new Error(`side ${side.name} asked for ${describeMode(want)} and the launch reports ${got ? describeMode(got) : "no renderer"}`);
    await cap.exec(c, "r_drawplayersprites 0");
    // [rc4l] Beside GL the backend derives its own scene, as players run it, unless the side is judged by GL's frame or by both renderers' from one frame (glDraws), which keeps GL drawing.
    if (glBeside) await cap.exec(c, `fua_dg_standalone ${side.frame === "gl" || side.glDraws ? 0 : 1}`);
    for (const line of sideCommands(side)) await cap.exec(c, line);
    await cap.waitTics(c, 10);
    return body(c, inst);
  });
}

// [rc4l] Which half of a Vulkan launch's GL/Vulkan pair a side keeps: Vulkan's, or with `frame: "gl"` GL's, drawn beside the backend's wall mesh.
export function pairFrame(side, pair) {
  return side.frame === "gl" ? pair.gl : pair.vk;
}

// [rc4l] Stand at a vantage and take one side's frame; GL alone comes from a Vulkan-off launch, the one reliable GL reference.
export async function grab(c, exe, tag, at, side) {
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
  const others = sides.some((sd) => sd.gl || sideBesideGl(sd)) ? engineProcessCount() : 0;
  if (others > 0) return { lines: [`${others} other engine window(s) open -- close them first`], failed: 1, refused: true };
  const exe = slashed(resolveEngine());
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
  if (opts.configs) return withConfigCvars(opts, sideName, cvars);
  const sides = opts.sides || GL_VS_VULKAN;
  if (!sides.some((s) => s.name === sideName)) throw new Error(`${opts.name} has no side ${sideName} (${sides.map((s) => s.name).join(", ")})`);
  return { ...opts, sides: sides.map((s) => (s.name === sideName ? { ...s, cvars: { ...(s.cvars || {}), ...cvars } } : s)) };
}

// [rc4l] A scenario's options with cvars added to every side, for one setting of a matrix run.
export function withEverySideCvars(opts, cvars) {
  return { ...opts, sides: (opts.sides || GL_VS_VULKAN).map((s) => ({ ...s, cvars: { ...(s.cvars || {}), ...cvars } })) };
}

// [rc4l] The cvar sets a --matrix "k=a,b" names, one per value.
export function matrixSets(text) {
  const m = /^([A-Za-z_][\w]*)=(.+)$/.exec(String(text));
  if (!m) throw new Error(`--matrix takes cvar=a,b, not ${text}`);
  return m[2].split(",").map((v) => ({ [m[1]]: v }));
}

// [rc4l] An adjustment with one matrix setting laid over whatever the run adjusts, a skipped or absent scenario left as it is.
export function matrixAdjust(cvars, adjust = (o) => o) {
  return (opts) => {
    const a = adjust(opts);
    return a && !a.skip ? withEverySideCvars(a, cvars) : a;
  };
}

// [rc4l] withSideCvars for a whole run: a scenario without that side gives null, to be skipped rather than end the run.
export function sideAdjust(sideName, cvars) {
  return (opts) => ((opts.configs || opts.sides || GL_VS_VULKAN).some((s) => s.name === sideName) ? withSideCvars(opts, sideName, cvars) : null);
}

// [rc4l] A netplay scenario's options with cvars added to one named configuration, on its server and its clients alike (a fault knob does nothing on the side it does not apply to).
export function withConfigCvars(opts, configName, cvars) {
  if (!opts.configs.some((c) => c.name === configName)) throw new Error(`${opts.name} has no configuration ${configName} (${opts.configs.map((c) => c.name).join(", ")})`);
  return { ...opts, configs: opts.configs.map((c) => (c.name === configName
    ? { ...c, serverCvars: { ...(c.serverCvars || {}), ...cvars }, clientCvars: { ...(c.clientCvars || {}), ...cvars } } : c)) };
}

// [rc4l] The mean |d| every box is held to when WebGPU is judged against Vulkan: the same backend over two APIs, so near the same picture.
export const WEBGPU_CEILING = 1.5;

// [rc4l] A scenario on WebGPU, by how it judges: two launches compare its first Vulkan side on each without GL, each box at WEBGPU_CEILING with no floor; one launch judged against itself (a counter, a picture over time) runs on WebGPU alone with its own limits; skipped, with the reason, when it judges GL and the backend in one frame or needs ray tracing; null with no Vulkan side of its own.
export function webgpuOptions(opts, compare = compareTemplate) {
  if (compare.netgame) return { skip: "it judges a netgame of locked Vulkan clients, which --webgpu does not change" };
  if (compare === compareSameFrame) return { skip: "it judges GL and the backend in one frame, and a WebGPU launch has no GL" };
  if ((opts.needs || []).includes("raytracing")) return { skip: "it needs ray tracing, which WebGPU lacks" };
  if ((opts.needs || []).includes("readback")) return { skip: "it reads each presented frame back at once, which WebGPU does only later" };
  const vk = (opts.sides || GL_VS_VULKAN).find((s) => !s.gl && s.frame !== "gl");
  if (!vk) return null;
  const { frame, glDraws, ...rest } = vk;
  if (compare.judgedAlone) return { ...opts, sides: [{ ...rest, name: "wgpu", noGl: true, webgpu: true }] };
  const boxes = Object.fromEntries(Object.entries(opts.boxes).map(([map, bs]) => [map, Object.fromEntries(Object.entries(bs).map(
    ([n, b]) => [n, { box: b.box, block: b.block, vantage: b.vantage, ceiling: WEBGPU_CEILING }]))]));
  return { ...opts, sides: [{ ...rest, name: "vk", noGl: true }, { ...rest, name: "wgpu", noGl: true, webgpu: true }], boxes };
}

// [rc4l] A quit judged by how the engine ended (launch.mjs's exited, null while it runs): a crash file beside its log, an engine still running, or an exit other than 0 fails it.
export function judgeQuit(label, { gone, crashed }) {
  if (crashed) return { line: `FAIL ${label}: the engine crashed on its way out`, failed: 1 };
  if (!gone) return { line: `FAIL ${label}: the engine did not exit`, failed: 1 };
  if (gone.code !== 0) return { line: `FAIL ${label}: the engine exited with ${gone.code ?? gone.signal}`, failed: 1 };
  return { line: `PASS ${label}: quit cleanly`, failed: 0 };
}

// [rc4l] Runs judgeLaunch(c, inst, label, { wadPath, iwad }) in a launch of each side on each map, collecting each { line, failed } verdict, and a launch that throws as a failure.
async function eachLaunch({ name, wad, maps, sides, iwad = FREEDOOM2, port = 7796, size = [1280, 720] }, judgeLaunch) {
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    for (const side of sides) {
      const label = `${map} ${side.name}`;
      try {
        const r = await withEngine({ port, map, iwad, wadPath, gl: !!side.gl, side, size }, (c, inst) => judgeLaunch(c, inst, label, { wadPath, iwad }));
        lines.push(r.line);
        failed += r.failed;
      } catch (e) {
        lines.push(`FAIL ${label}: ${e.message}`);
        failed++;
      }
    }
  }
  return { lines, failed };
}

// [rc4l] Each side launched on each map, played a moment and quit from its console as a player quits, then judged by judgeQuit: for a fault on the engine's way out, which no frame shows.
export function compareQuit(opts) {
  return eachLaunch(opts, async (c, inst, label) => {
    await cap.waitTics(c, opts.settleTics ?? 70);
    // [rc4l] Over the scenario's own connection, whose answer never comes since the engine leaves first.
    await c.rpc("console.exec", { text: "quit" }).catch(() => {});
    const gone = await exited(inst.proc, 15000);
    return judgeQuit(label, { gone, crashed: fs.existsSync(`${inst.log}.crash`) });
  });
}

// [rc4l] Each side launched on each map and judged by the scenario's own judgeLaunch(c, inst, label), for a check one bridge answer settles.
export function compareEachLaunch(opts) {
  return eachLaunch(opts, (c, inst, label) => opts.judgeLaunch(c, inst, label));
}

// [rc4l] Each map hosted by a server with the scenario's wad and joined by a client of each side playing it, both locked, judged together by judgeHosted(server, client, label): for what a netgame splits between the two, such as what only a client draws.
export async function compareHosted({ name, wad, maps, sides = [{ name: "vk" }], iwad = FREEDOOM2, port = 7796, gamePort = 10790, judgeHosted }) {
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) for (const side of sides) {
    const label = `${map} ${side.name} hosted`;
    const insts = [], conns = [];
    try {
      // [rc4l] -port on the command line, since a +port cvar is ignored, and cheats set at launch, since a later change never reaches a joined client.
      const server = await launchInstance({ port, iwad, map, extraArgs: ["-file", wadPath, "-host", "-port", String(gamePort), "+sv_broadcast", "0", "+sv_updatemaster", "0", "+sv_cheats", "1"] });
      insts.push(server);
      const sc = await attach(server);
      conns.push(sc);
      await cap.waitInLevel(sc, scaled(60000));
      const client = await launchInstance({ port: port + 1, iwad, extraArgs: ["-file", wadPath, ...(side.webgpu ? ["-webgpu"] : [])], connect: `127.0.0.1:${gamePort}`, cvars: { fua_render_in_background: 1 } });
      insts.push(client);
      const cc = await attach(client);
      conns.push(cc);
      await cap.waitInLevel(cc, scaled(60000));
      const want = launchMode({ webgpu: !!side.webgpu });
      const got = cap.launchModeOf(await cap.exec(cc, cap.RENDERER_PROBE, { quietMs: 300 }));
      if (got !== want) throw new Error(`side ${side.name} asked for ${describeMode(want)} and the client reports ${got ? describeMode(got) : "no renderer"}`);
      // [rc4l] A client arrives a spectator, which neither moves nor fires.
      await cap.exec(cc, "join");
      await cap.waitTics(cc, 35);
      const r = await judgeHosted(sc, cc, label);
      lines.push(r.line);
      failed += r.failed;
    } catch (e) {
      const crashed = insts.find((i) => fs.existsSync(`${i.log}.crash`));
      lines.push(`FAIL ${label}: ${crashed ? `engine crashed -- ${crashed.log}.crash` : e.message}`);
      failed++;
    } finally {
      for (const c of conns) c.close();
      for (const i of insts.reverse()) await stopInstance(i);
    }
  }
  return { lines, failed };
}

// [rc4l] Where the engine put a window pixel (input.windowpoint) against the render pixel under it, failing too on a render size that says the scale never took.
export function judgeWindowPoint(label, got, want) {
  const ok = got.width === want.width && got.height === want.height && Math.abs(got.x - want.x) <= 1 && Math.abs(got.y - want.y) <= 1;
  const text = `window ${want.from.join(",")} at render ${got.x},${got.y} of ${got.width}x${got.height}, want ${want.x},${want.y} of ${want.width}x${want.height}`;
  return { line: `${ok ? "PASS" : "FAIL"} ${label}: ${text}`, failed: ok ? 0 : 1 };
}

// [rc4l] Menu reads (ui.mjs readMenu) before a row's value changed, after, and after changing back, passing when note sits clear of the row's text in its band only while changed and no absent text shows.
export function judgeNoteBeside(label, { before, changed, back }, { row, note, absent = [] }) {
  const texts = (read) => read.texts || [];
  const find = (read, text) => texts(read).find((t) => t.text.includes(text));
  const problems = [];
  const r = find(changed, row);
  const n = find(changed, note);
  if (!r) problems.push(`no "${row}" row`);
  else if (!n) problems.push("no note after the change");
  else {
    // [rc4l] A row's band runs from its top to the next row's, so a smaller note centred on it is still beside it.
    const next = Math.min(Infinity, ...texts(changed).filter((t) => t !== n && t.y > r.y + 3).map((t) => t.y));
    if (n.y < r.y - 3 || n.y >= next || n.x <= r.x) problems.push("the note is on a line of its own");
    else if (texts(changed).some((t) => t !== n && t.y >= r.y - 3 && t.y < next && t.x < n.x && t.x + t.w > n.x)) problems.push("the note overlaps the row");
  }
  if (find(before, note)) problems.push("the note shows before any change");
  if (find(back, note)) problems.push("the note still shows after changing back");
  for (const text of absent) if ([before, changed, back].some((read) => find(read, text))) problems.push(`"${text}" still shows`);
  return problems.length ? { line: `FAIL ${label}: ${problems.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: "${note}" beside "${row}" only while changed`, failed: 0 };
}

// [rc4l] A soak's verdict (telemetry.mjs runSoak) as a scenario line: what kept growing and by how much a cycle, as the engine says, or how many counters held.
export function judgeSoak(label, { pass, marks, judged, grew = [] }) {
  if (pass) return { line: `PASS ${label}: ${judged} counters held over ${marks} marks`, failed: 0 };
  return { line: `FAIL ${label}: ${grew.length ? `kept growing: ${grew.map((g) => g.text).join("; ")}` : `judged nothing (${marks} marks)`}`, failed: 1 };
}

// [rc4l] Each side launched on each map repeats the scenario's soak action (reload unless it names another, or builds one from the launch's { wadPath, iwad }) and passes when the engine judges that no memory, object, handle or thread count kept growing: for a leak, which no frame shows.
export function compareSoak(opts) {
  return eachLaunch(opts, async (c, inst, label, files) => {
    await cap.waitTics(c, opts.settleTics ?? 35);
    const action = typeof opts.action === "function" ? opts.action(files) : opts.action ?? "reload";
    return judgeSoak(label, await runSoak(c, { action, cycles: opts.cycles ?? 6, warmup: opts.warmup ?? 1, settleMs: opts.settleMs ?? 4000, ignore: opts.ignore ?? [] }));
  });
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

// [rc4l] An imported scenario as one call that runs its comparison (its options passed through adjust, with the comparison) and returns its outcome, or null if the module registered none; an adjust giving null, or { skip: reason }, skips it.
export function scenarioRun(metaUrl, adjust = (o) => o) {
  const e = registered.get(metaUrl);
  if (!e) return null;
  return async () => {
    const base = e.options();
    const opts = adjust(base, e.compare);
    if (!opts || opts.skip) {
      const why = opts ? opts.skip : `no side to run (${(base.sides || GL_VS_VULKAN).map((s) => s.name).join(", ")})`;
      return { lines: [`skipped: ${why}`], code: 0, skipped: true };
    }
    return scenarioOutcome(opts.name, await e.compare(opts));
  };
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

// [rc4l] A counter the engine printed, held to its ceiling or its floor: the number pattern's first group captures in text, as a PASS/FAIL line and a failure count.
export function judgeCounter(map, text, { label, pattern, ceiling, floor }) {
  const m = new RegExp(pattern).exec(String(text));
  if (!m) return { line: `FAIL ${map}: no ${label} in the engine's answer`, failed: 1 };
  const value = Number(m[1]);
  const ok = (ceiling === undefined || value <= ceiling) && (floor === undefined || value >= floor);
  const limit = ceiling !== undefined ? `ceiling ${ceiling}` : `floor ${floor}`;
  return { line: `${ok ? "PASS" : "FAIL"} ${map} ${label}: ${value} (${limit})`, failed: ok ? 0 : 1 };
}

// [rc4l] One Vulkan launch of the wad per map, body(c, map) giving that map's { lines, failed }, and a launch that throws one FAIL line: the shape the comparisons below share.
async function eachVulkanLaunch({ name, wad, maps, iwad, port, size, side }, body) {
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    try {
      const r = await withEngine({ port, map, iwad, wadPath, gl: false, side, size }, (c) => body(c, map));
      lines.push(...r.lines);
      failed += r.failed;
    } catch (e) {
      lines.push(`FAIL ${map}: ${e.message}`);
      failed++;
    }
  }
  return { lines, failed };
}

// [rc4l] One Vulkan launch per map, standing at each vantage with the sim running, then counter.command's number judged by judgeCounter: for a fault no paused frame can show, such as a one-frame flash the engine counts; the launch takes the first non-GL side's cvars.
export async function compareCounter({ name, wad, maps, vantages, counter, sides = [], iwad = FREEDOOM2, port = 7796, size = [1280, 720] }) {
  return eachVulkanLaunch({ name, wad, maps, iwad, port, size, side: sides.find((s) => !s.gl) }, async (c, map) => {
    for (const v of vantages) {
      await c.rpc("sim.resume", {}).catch(() => {});
      await c.rpc("player.setpos", v.at);
      await cap.waitTics(c, 35);
    }
    const r = judgeCounter(map, await cap.exec(c, counter.command, { quietMs: 400 }), counter);
    return { lines: [r.line], failed: r.failed };
  });
}

// [rc4l] GL against Vulkan from ONE frame of one Vulkan launch, for what moves with time (a scroller, an animation), where two launches never agree on the moment; `commands` run once the level is up, as a side's do.
export async function compareSameFrame({ name, wad, maps, vantages, boxes, iwad = FREEDOOM2, port = 7796, size = [1280, 720], settleTics = 70, commands = [] }) {
  const others = engineProcessCount();
  if (others > 0) return { lines: [`${others} other engine window(s) open -- close them first`], failed: 1, refused: true };
  const exe = slashed(resolveEngine());
  return eachVulkanLaunch({ name, wad, maps, iwad, port, size, side: { name: "pair", glDraws: true, commands } }, async (c, map) => {
    const frames = {};
    await cap.waitTics(c, settleTics);
    for (const v of vantages) {
      await c.rpc("sim.resume", {}).catch(() => {});
      await c.rpc("player.setpos", v.at);
      await cap.waitTics(c, 15);
      const pair = await shot.shotPair(c, `${name}_${map}_${v.name}`, { engineBin: exe, sameFrame: true });
      frames[v.name] = { gl: pair.gl ? decodePNG(pair.gl) : null, vk: pair.vk ? decodePNG(pair.vk) : null };
    }
    return judge(map, frames, boxes[map], size);
  });
}

// [rc4l] One Vulkan launch per map; at each vantage a capture after each of `steps` (shot.captureSequence), and each box judged between the two captures its `pair` names: for what a command must change (a mark made) and what must survive one (a mode change).
export async function compareSequence({ name, wad, maps, vantages, steps, boxes, iwad = FREEDOOM2, port = 7796, size = [1280, 720], sides = [] }) {
  const exe = slashed(resolveEngine());
  return eachVulkanLaunch({ name, wad, maps, iwad, port, size, side: sides.find((s) => !s.gl) }, async (c, map) => {
    const frames = {};
    for (const v of vantages) {
      const { vk } = await shot.captureSequence(c, { at: v.at, engineBin: exe, tag: `${name}_${map}_${v.name}`, steps });
      frames[v.name] = Object.fromEntries(Object.entries(vk).map(([step, file]) => [step, file ? decodePNG(file) : null]));
    }
    const [first, second] = steps.map((s) => s.name);
    return judge(map, frames, boxes[map], size, [first, second]);
  });
}

// [rc4l] One Vulkan launch per map, each vantage captured twice intervalMs apart, judged before against after: for what must move (a warp) or must not; the launch takes the first non-GL side's settings.
export async function compareOverTime({ name, wad, maps, vantages, boxes, intervalMs = 600, iwad = FREEDOOM2, port = 7796,
  size = [1280, 720], sides = [] }) {
  const exe = slashed(resolveEngine());
  return eachVulkanLaunch({ name, wad, maps, iwad, port, size, side: sides.find((s) => !s.gl) }, async (c, map) => {
    const frames = {};
    for (const v of vantages) {
      const { vk: [before, after] } = await shot.captureOverTime(c, { at: v.at, engineBin: exe,
        tag: `${name}_${map}_${v.name}`, intervalMs });
      frames[v.name] = { before: before ? decodePNG(before) : null, after: after ? decodePNG(after) : null };
    }
    return judge(map, frames, boxes[map], size, ["before", "after"]);
  });
}

// [rc4l] The comparisons that judge a launch against itself rather than GL, which a WebGPU run runs on WebGPU alone (webgpuOptions).
// [rc4l] Each netplay configuration (a name and its players, observers, per-client lag and loss, client cvars) hosted on each map and judged by judgeNetplay(set, label, { wadPath, iwad, map, config }) with the whole set: for what a netgame splits between a server, the players and those watching them.
export async function compareNetplay({ name, wad, maps, configs, iwad = FREEDOOM2, port = 7840, gamePort = 10840, judgeNetplay }, deps = {}) {
  const { launch = launchNetplay, close = closeNetplay, seams = seamViolations } = deps;
  const wadPath = path.join(os.tmpdir(), `fua-${name}-template.wad`);
  fs.writeFileSync(wadPath, wad);
  const lines = [];
  let failed = 0;
  for (const map of maps) for (const config of configs) {
    const label = `${map} ${config.name}`;
    let set = null;
    try {
      set = await launch({ iwad, map, files: [wadPath], players: config.players ?? 1, observers: config.observers ?? 0, lag: config.lag ?? "", loss: config.loss ?? "",
        basePort: port, gamePort, cvars: config.serverCvars || {}, clientCvars: config.clientCvars || {}, broadcast: !!config.broadcast });
      const r = await judgeNetplay(set, label, { wadPath, iwad, map, config });
      lines.push(...(r.lines || [r.line]));
      failed += r.failed;
      // [rc4l] And the netcode stayed on the threads its seams name, on every instance.
      const crossed = await seams(set);
      if (crossed.length) { lines.push(`FAIL ${label}: ${crossed.join("; ")}`); failed++; }
    } catch (e) {
      lines.push(`FAIL ${label}: ${e.message}`);
      failed++;
    } finally {
      if (set) await close(set);
    }
  }
  return { lines, failed };
}
compareNetplay.netgame = true;

for (const compare of [compareCounter, compareOverTime, compareSequence, compareQuit, compareSoak, compareEachLaunch, compareHosted, compareNetplay]) compare.judgedAlone = true;
