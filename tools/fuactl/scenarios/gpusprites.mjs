// [Doruk-Aksoy] The tic stream (fua_dg_gpusprites) against the CPU path: one held tic of moving actors drawn both ways at several fractions and cameras, every pair the same picture to the noise.
//
// usage:  node scenarios/gpusprites.mjs [port] [--engine path], or fuactl scenarios gpusprites [--matrix k=a,b]; GPUSPRITES_MAPS=MAP01,MAP05 runs some maps
// Each map runs to its tic, holds it (fua_debug_holdtics), and for each camera (fua_debug_viewyaw/dx/dy) and fraction (fua_debug_ticfrac)
// captures fua_dg_gpusprites 1 then 0; a pair whose worst 16-pixel block (mean |d| per pixel) passes the ceiling fails. Then, still held,
// settings a menu changes while paused are changed after the stream is made, each judged the same way; then tics run, and the stream
// must be made no more often than tics are filled, and no camera texture's pass may draw it.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, FREEDOOM2 } from "../src/template.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import { kvxBox, voxelLumps } from "../src/kvx.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

export const FRACTIONS = [0, 250, 500, 750, 1000];
// [Doruk-Aksoy] Still, turned so actors at the frustum's edge come and go, and moved so each actor is seen from another side.
export const CAMERAS = [
  { name: "still", yaw: 0, dx: 0, dy: 0 },
  { name: "turn", yaw: 25, dx: 0, dy: 0 },
  { name: "turnback", yaw: -30, dx: 0, dy: 0 },
  { name: "strafe", yaw: 0, dx: 48, dy: 0 },
  { name: "advance", yaw: 0, dx: 0, dy: 96 },
];
// [Doruk-Aksoy] The worst block either path may differ by: a template map draws the same pixels both ways, so this is float noise only.
export const CEILING = 4;
export const CVARS = { vid_maxfps: 0, vid_vsync: 0, i_pauseinbackground: 0, fua_render_in_background: 1, snd_sfxvolume: 0, snd_musicvolume: 0, show_messages: 0, fua_debug_frameclock: 5000 };

export const TURRET = 30600;   // [Doruk-Aksoy] the first turret's editor number, one class a missile and firing period

// [Doruk-Aksoy] Projectiles whose look the tic stream must leave to the CPU, or carry exactly: rolled, XY-billboarded, subtracted (a fog layer under fog) and shadowed.
export const MISSILES = {
  FuaRollBall: "\tRadius 6\n\tHeight 8\n\tSpeed 14\n\tDamage 0\n\tProjectile\n\t+ROLLSPRITE\n\tStates\n\t{\n\tSpawn:\n\t\tMISL A 1 A_SetRoll(0)\n\t\tMISL A 1 A_SetRoll(40)\n\t\tMISL A 1 A_SetRoll(80)\n\t\tMISL A 1 A_SetRoll(120)\n\t\tLoop\n\tDeath:\n\t\tMISL B 8 Bright\n\t\tStop\n\t}",
  FuaXYBall: "\tRadius 6\n\tHeight 8\n\tSpeed 12\n\tDamage 0\n\tProjectile\n\t+FORCEXYBILLBOARD\n\tStates\n\t{\n\tSpawn:\n\t\tBAL1 AB 4 Bright\n\t\tLoop\n\tDeath:\n\t\tBAL1 CDE 6 Bright\n\t\tStop\n\t}",
  FuaSubBall: "\tRadius 6\n\tHeight 8\n\tSpeed 10\n\tDamage 0\n\tProjectile\n\tRenderStyle Subtract\n\tStates\n\t{\n\tSpawn:\n\t\tBAL2 AB 4\n\t\tLoop\n\tDeath:\n\t\tBAL2 CDE 6\n\t\tStop\n\t}",
  FuaShadowBall: "\tRadius 6\n\tHeight 8\n\tSpeed 11\n\tDamage 0\n\tProjectile\n\tRenderStyle Shadow\n\tStates\n\t{\n\tSpawn:\n\t\tBAL7 AB 4\n\t\tLoop\n\tDeath:\n\t\tBAL7 CDE 6\n\t\tStop\n\t}",
};
// [Doruk-Aksoy] A soulsphere that bobs, whose bob is no straight line across a tic.
export const BOBBER = 30690;
// [Doruk-Aksoy] A still, additive, fullbright imp ball in view, which r_usecolorblending blends by its own colour whatever the monsters fire.
export const GLOWER = 30691;

// [Doruk-Aksoy] Unseen turrets firing straight ahead, Doom's rocket (eight rotations) or one of MISSILES, each class on its own period so the shots never line up.
export const TURRETS = [["Rocket", 7], ["Rocket", 9], ["Rocket", 11], ["Rocket", 13], ["FuaRollBall", 8], ["FuaXYBall", 10], ["FuaSubBall", 12], ["FuaShadowBall", 9]];
const turret = ([missile, period], i) => `actor FuaGpuTurret${i} ${TURRET + i}
{
\tRadius 4
\tHeight 8
\t+NOGRAVITY
\t+NOBLOCKMAP
\t+NOTELEPORT
\t+DONTSPLASH
\tStates
\t{
\tSpawn:
\t\tTNT1 A ${period} A_CustomMissile("${missile}", 0, 0, 0, CMF_AIMDIRECTION)
\t\tLoop
\t}
}
`;
export const DECORATE = [
  ...Object.entries(MISSILES).map(([name, body]) => `actor ${name}\n{\n${body}\n}\n`),
  `actor FuaBobber ${BOBBER}\n{\n\t+FLOATBOB\n\t+NOGRAVITY\n\tStates\n\t{\n\tSpawn:\n\t\tSOUL ABCD 6 Bright\n\t\tLoop\n\t}\n}\n`,
  `actor FuaGlower ${GLOWER}\n{\n\t+NOGRAVITY\n\tRenderStyle Add\n\tScale 2\n\tStates\n\t{\n\tSpawn:\n\t\tBAL1 A -1 Bright\n\t\tStop\n\t}\n}\n`,
  ...TURRETS.map(turret),
].join("\n");
// [Doruk-Aksoy] Nukage glows, so a sprite over it takes light by its height.
export const GLDEFS = "glow\n{\n\tflats\n\t{\n\t\tNUKAGE1\n\t}\n}\n";

const ROOM = { heightfloor: 0, heightceiling: 512, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 };
const WALL = () => ({ side: { texturemiddle: "STARTAN2" } });
const STEP = () => ({ texturebottom: "STARTAN2", texturetop: "STARTAN2" });

// [Doruk-Aksoy] The player 500 short of a pen fenced by monster-blocking lines, its monsters awake, and turrets on both side walls firing across the view from 120 to 700 ahead.
function arena(m, room, { monsters, turrets = [0, 1, 2, 3], pen = { ...ROOM } }) {
  const penSector = m.block(512, 900, 1536, 1600, pen, room, () => ({}), () => ({}), () => ({ blockmonsters: true }));
  let k = 0;
  for (const [type, n] of monsters)
    for (let i = 0; i < n; i++, k++) m.thing({ x: 600 + (k * 137) % 860, y: 960 + ((k * 89) % 560), angle: (k * 45) % 360, type });
  m.thing({ x: 1024, y: 400, angle: 90, type: 1 });
  [520, 620, 760, 900, 1100].forEach((y, i) => {
    m.thing({ x: 40, y, height: 40 + i * 8, angle: 0, type: TURRET + turrets[i % turrets.length] });
    m.thing({ x: 2008, y: y + 50, height: 56 + i * 8, angle: 180, type: TURRET + turrets[(i + 2) % turrets.length] });
  });
  return penSector;
}

// [Doruk-Aksoy] MAP01, motion: monsters walking and firing, rockets crossing near and far, a particle fountain and a still additive ball among them.
export function motionMap() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 2048, 2048, ROOM, WALL);
  arena(m, room, { monsters: [[3001, 6], [3004, 4], [3002, 3], [3005, 2], [66, 2]] });
  m.thing({ x: 1300, y: 860, type: 9027 });
  m.thing({ x: 900, y: 640, height: 48, type: GLOWER });
  return m;
}

// [Doruk-Aksoy] MAP02, terrain: a ziggurat of 16-unit steps, a slope, glowing nukage and a glass 3D floor inside the pen, so walkers are clipped, glow, slide uphill and pass under a plane.
export function terrainMap() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 2048, 2048, ROOM, WALL);
  const pen = arena(m, room, { monsters: [[3001, 8], [3004, 6], [3002, 4], [3005, 2]] });
  let outer = pen;
  for (let s = 1; s <= 4; s++) outer = m.block(560 + s * 32, 960 + s * 32, 900 - s * 32, 1540 - s * 32, { ...ROOM, heightfloor: s * 16 }, outer, STEP, STEP);
  m.block(940, 1000, 1140, 1300, { ...ROOM, heightfloor: 64 }, pen, STEP, () => ({}), (edge) => (edge === "south" ? { special: SPECIAL.Plane_Align, arg0: 1 } : {}));
  m.block(1200, 960, 1480, 1200, { ...ROOM, texturefloor: "NUKAGE1" }, pen);
  m.block(1200, 1260, 1480, 1540, { ...ROOM, id: 7 }, pen);
  m.room(2200, 0, 2264, 64, { heightfloor: 96, heightceiling: 104, texturefloor: "FLAT14", textureceiling: "FLAT14", lightlevel: 255 },
    (edge) => (edge === "west" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: 7, arg1: 1, arg2: 0, arg3: 128 } } : WALL()));
  return m;
}

// [Doruk-Aksoy] MAP03, styles: grey fog, a mirror for the west wall, bobbing spheres, and turrets firing rolled, XY-billboarded, subtracted and shadowed balls.
export function stylesMap() {
  const m = new UdmfMap();
  const fogged = { ...ROOM, lightlevel: 200, fadecolor: 0x606060 };
  const room = m.room(0, 0, 2048, 2048, fogged,
    (edge) => (edge === "west" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } } : WALL()));
  arena(m, room, { monsters: [[3001, 5], [3002, 3]], turrets: [4, 5, 6, 7, 0], pen: { ...fogged } });
  for (let i = 0; i < 5; i++) m.thing({ x: 800 + i * 110, y: 700 + (i % 2) * 60, type: BOBBER });
  return m;
}

// [Doruk-Aksoy] MAP04, models: lost souls drawn as voxel boxes among imp sprites, and particle fountains.
export function modelsMap() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 2048, 2048, ROOM, WALL);
  arena(m, room, { monsters: [[3006, 6], [3001, 6]] });
  [9027, 9028, 9029].forEach((type, i) => m.thing({ x: 800 + i * 220, y: 820, type }));
  return m;
}

// [Doruk-Aksoy] MAP05, cameras: dormant cacodemons ahead of the player, which float clear of the floor so the tic stream carries them, and a screen showing them from the far side, whose pass draws its own view's sprites and none of the tic stream.
export const CAMERA_TID = 5;
export const ANIMDEFS = "cameratexture GPUCAM 256 128 fit 1536 160\n";
export function camerasMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1536, 1024, { ...ROOM, heightceiling: 160 }, (edge) => ({ side: { texturemiddle: edge === "north" ? "GPUCAM" : "STARTAN2" } }));
  m.thing({ x: 768, y: 128, angle: 90, type: 1 });
  m.thing({ x: 768, y: 1000, height: 40, angle: 270, type: 9001, id: CAMERA_TID });
  for (const x of [400, 480, 1056, 1136]) m.thing({ x, y: 600, height: 24, angle: 90, type: 3005, dormant: true });
  return m;
}
// [Doruk-Aksoy] SetCameraToTexture(CAMERA_TID, "GPUCAM", 90) from an OPEN script.
const cameraScript = () => acs0([PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE, PCD.TERMINATE], ["GPUCAM"]);

// [Doruk-Aksoy] Settings a paused game's menu changes between tics, each made after the stream is and reverted after.
export const SETTINGS = {
  colorblending: { change: ["r_usecolorblending 0"], revert: ["r_usecolorblending 1"] },
  spriteclip2: { change: ["r_spriteclip 2"], revert: ["r_spriteclip 1"] },
  spriteclip3: { change: ["r_spriteclip 3"], revert: ["r_spriteclip 1"] },
  sclipthreshold: { change: ["r_sclipthreshold 1"], revert: ["r_sclipthreshold 10"] },
};

// [Doruk-Aksoy] Each map's held tic, the player's pitch where one is wanted, whether the own-missile phase runs there, the settings changed while held, and its script.
export const MAPS = {
  MAP01: { build: motionMap, tic: 210, fire: true, settings: ["colorblending"] },
  MAP02: { build: terrainMap, tic: 280, settings: ["spriteclip2", "spriteclip3", "sclipthreshold"] },
  MAP03: { build: stylesMap, tic: 210, pitch: 12 },
  MAP04: { build: modelsMap, tic: 210 },
  MAP05: { build: camerasMap, tic: 70, behavior: cameraScript, cameras: true },
};

export function buildGpuSpritesWad() {
  return wad([{ name: "DECORATE", data: DECORATE }, { name: "GLDEFS", data: GLDEFS }, { name: "ANIMDEFS", data: ANIMDEFS },
    ...voxelLumps("SKUL", kvxBox(20, 20, 40, { color: [63, 40, 0] })),
    ...Object.entries(MAPS).flatMap(([name, def]) => mapLumps(name, def.build(), def.behavior ? { behavior: def.behavior() } : {}))]);
}

// [Doruk-Aksoy] Two decoded frames' 16-pixel blocks by mean |d| per pixel and channel, worst first, with their positions.
export function worstBlocks(a, b, n = 3, block = 16) {
  const out = [];
  for (let by = 0; by < a.h; by += block) for (let bx = 0; bx < a.w; bx += block) {
    let s = 0, k = 0;
    for (let y = by; y < Math.min(by + block, a.h); y++) for (let x = bx; x < Math.min(bx + block, a.w); x++)
      for (let ch = 0; ch < 3; ch++) { const i = (y * a.w + x) * a.channels + ch; s += Math.abs(a.data[i] - b.data[(y * b.w + x) * b.channels + ch]); k++; }
    out.push({ d: s / k, x: bx, y: by });
  }
  return out.sort((p, q) => q.d - p.d).slice(0, n);
}

// [Doruk-Aksoy] fua_dg_ticstats's line as numbers by name, null when it printed none.
export function parseTicStats(text) {
  const m = /ticstats pieces (\d+) packed (\d+) unpacked (\d+) hidden (\d+) shown (\d+) framederived (\d+) cameraticdraws (\d+) builds (\d+) fill (\d+)/.exec(text || "");
  if (!m) return null;
  const [pieces, packed, unpacked, hidden, shown, frameDerived, cameraTicDraws, builds, fill] = m.slice(1).map(Number);
  return { pieces, packed, unpacked, hidden, shown, frameDerived, cameraTicDraws, builds, fill };
}

// [Doruk-Aksoy] The verdict on a map's pairs: each pair's worst block against the ceiling, and the shader path having drawn sprites for it, since two CPU pictures always match.
export function judgeFractions(pairs, ceiling = CEILING) {
  const failed = pairs.filter((p) => !(p.worst.length && p.worst[0].d <= ceiling) || !(p.stats && p.stats.shown > 0));
  return { pass: failed.length === 0, failed };
}

// [Doruk-Aksoy] A setting's pair: the shader and CPU pictures the same to the noise, and the CPU picture moved by the change, or the step tests nothing.
export function judgeSetting({ worst, moved, stats }, ceiling = CEILING) {
  return worst.length > 0 && worst[0].d <= ceiling && moved > ceiling && !!stats && stats.shown > 0;
}

// [Doruk-Aksoy] A run of free tics: the stream made at most once a fill (and once for switching on), and no camera pass drawing it.
export function judgeRun(before, after) {
  if (!before || !after) return { ok: false, why: "no ticstats" };
  const fills = after.fill - before.fill;
  if (after.cameraTicDraws !== 0) return { ok: false, why: `camera passes drew ${after.cameraTicDraws} tic runs` };
  if (fills < 10) return { ok: false, why: `only ${fills} fills ran` };
  if (after.builds > fills + 1) return { ok: false, why: `the stream was made ${after.builds} times over ${fills} fills` };
  return { ok: true, why: `made ${after.builds} times over ${fills} fills, no tic run in a camera pass` };
}

// [Doruk-Aksoy] Tics the own-missile phase holds in turn after +attack, enough for the rocket launcher's fire frame and the rocket's first tics.
export const FIRE_TICS = 14;

// [Doruk-Aksoy] One map: run to its tic, hold it, capture every camera and fraction both ways; then, where the map asks, fire a rocket and hold each tic after; the pairs with their worst blocks.
export async function runMap({ port, engine, map, def, outDir, cvars = {} }) {
  const { launchInstance, stopInstance } = await import("../src/launch.mjs");
  const { attach } = await import("../src/session.mjs");
  const { exec, sleep, waitInLevel } = await import("../src/capture.mjs");
  const { captureVulkan } = await import("../src/shot.mjs");
  const { decodePNG } = await import("../src/pngcore.mjs");
  const wadPath = path.join(outDir, "gpusprites.wad");
  if (!fs.existsSync(wadPath)) fs.writeFileSync(wadPath, buildGpuSpritesWad());
  const inst = await launchInstance({ port, engine, map, iwad: FREEDOOM2, focus: false, cvars: { ...CVARS, ...cvars }, extraArgs: ["-file", wadPath] });
  const c = await attach(inst);
  const say = (t) => exec(c, t, { quietMs: 150 });
  const tic = async () => { const r = await c.rpc("sim.tic"); return (r.result || r).gametic; };
  const pairs = [], checks = [];
  // [Doruk-Aksoy] The held tic drawn from each camera at each fraction, the shader path then the CPU path, judged pair by pair.
  const captureBoth = async (label, cameras, fractions) => {
    const held = await tic();
    const shots = {}, stats = {};
    for (const mode of [1, 0]) {
      await say(`fua_dg_gpusprites ${mode}`);
      for (const cam of cameras) {
        await say(`fua_debug_viewyaw ${cam.yaw}`); await say(`fua_debug_viewdx ${cam.dx}`); await say(`fua_debug_viewdy ${cam.dy}`);
        for (const f of fractions) {
          await say(`fua_debug_ticfrac ${f}`);
          await sleep(300);
          const file = path.join(outDir, `${map}-${label}${cam.name}-${f}-${mode ? "gpu" : "cpu"}.png`);
          if (!(await captureVulkan(c, file))) throw new Error(`no capture ${file}`);
          shots[`${cam.name}/${f}/${mode}`] = file;
          if (mode) stats[`${cam.name}/${f}`] = parseTicStats(await exec(c, "fua_dg_ticstats", { quietMs: 200 }));
        }
      }
    }
    if ((await tic()) !== held) throw new Error(`the tic moved from ${held} while held`);
    for (const cam of cameras) for (const f of fractions) {
      const a = decodePNG(shots[`${cam.name}/${f}/1`]), b = decodePNG(shots[`${cam.name}/${f}/0`]);
      pairs.push({ map, camera: `${label}${cam.name}`, fraction: f, worst: worstBlocks(a, b), stats: stats[`${cam.name}/${f}`] });
    }
  };
  try {
    await waitInLevel(c, 120000);
    await say("god");
    if (def.pitch !== undefined) await c.rpc("player.setpos", { x: 1024, y: 400, z: 0, angle: 90, pitch: def.pitch });
    await say("fua_dg_gpusprites 1");
    for (let i = 0; i < 60000 && (await tic()) < def.tic; i++) await sleep(2);
    await say("fua_debug_holdtics 1");
    await sleep(400);
    await captureBoth("", CAMERAS, FRACTIONS);
    if (def.fire) {
      // [Doruk-Aksoy] The player's own rocket, hidden within its speed of the view: each tic after the trigger is held and judged from the still camera.
      await say("fua_debug_viewyaw 0"); await say("fua_debug_viewdx 0"); await say("fua_debug_viewdy 0");
      await say("give rocketlauncher");
      await say("use RocketLauncher");
      await say("fua_debug_holdtics 0");
      await sleep(1500);
      await say("fua_debug_holdtics 1");
      await sleep(300);
      await say("+attack");
      for (let n = 0; n < FIRE_TICS; n++) {
        const before = await tic();
        await say("fua_debug_steptic");
        for (let i = 0; i < 2000 && (await tic()) === before; i++) await sleep(2);
        await sleep(200);
        await captureBoth(`fire${n}-`, [CAMERAS[0]], [0, 500, 1000]);
      }
      await say("-attack");
    }
    // [Doruk-Aksoy] Each setting changed after the stream is made, the tic still held as a paused game holds it.
    await say("fua_debug_viewyaw 0"); await say("fua_debug_viewdx 0"); await say("fua_debug_viewdy 0"); await say("fua_debug_ticfrac 500");
    await say("fua_dg_gpusprites 0");
    await sleep(300);
    const base = path.join(outDir, `${map}-setting-base.png`);
    if (!(await captureVulkan(c, base))) throw new Error(`no capture ${base}`);
    for (const name of def.settings || []) {
      const step = SETTINGS[name];
      await say("fua_dg_gpusprites 1");
      await sleep(400);
      for (const cmd of step.change) await say(cmd);
      await sleep(400);
      const gpu = path.join(outDir, `${map}-setting-${name}-gpu.png`), cpu = path.join(outDir, `${map}-setting-${name}-cpu.png`);
      if (!(await captureVulkan(c, gpu))) throw new Error(`no capture ${gpu}`);
      const stats = parseTicStats(await exec(c, "fua_dg_ticstats", { quietMs: 200 }));
      await say("fua_dg_gpusprites 0");
      await sleep(400);
      if (!(await captureVulkan(c, cpu))) throw new Error(`no capture ${cpu}`);
      for (const cmd of step.revert) await say(cmd);
      const worst = worstBlocks(decodePNG(gpu), decodePNG(cpu));
      const moved = worstBlocks(decodePNG(cpu), decodePNG(base))[0].d;
      const ok = judgeSetting({ worst, moved, stats });
      checks.push({ map, name: `setting ${name}`, ok, why: `worst ${worst[0].d.toFixed(1)}, the change moved the picture ${moved.toFixed(1)}` });
    }
    // [Doruk-Aksoy] Then tics run free, with any camera screen on the map in view.
    await say("fua_dg_gpusprites 1");
    await say("fua_debug_ticfrac -1");
    await sleep(300);
    const before = parseTicStats(await exec(c, "fua_dg_ticstats", { quietMs: 200 }));
    await say("fua_debug_holdtics 0");
    await sleep(1500);
    const after = parseTicStats(await exec(c, "fua_dg_ticstats", { quietMs: 200 }));
    const run = judgeRun(before, after);
    checks.push({ map, name: "free tics", ok: run.ok, why: run.why });
  } finally {
    c.close();
    await stopInstance(inst);
  }
  return { pairs, checks };
}

// [Doruk-Aksoy] Every map (or GPUSPRITES_MAPS), the cvars of the run's first side on every launch, so --matrix reaches it; its frames kept in a temp folder named in its last line.
export async function compareGpuSprites({ port = 7874, sides } = {}) {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "gpusprites-"));
  const cvars = (sides && sides[0] && sides[0].cvars) || {};
  const maps = process.env.GPUSPRITES_MAPS ? process.env.GPUSPRITES_MAPS.split(",") : Object.keys(MAPS);
  const lines = [];
  let failed = 0;
  for (const map of maps) {
    const { pairs, checks } = await runMap({ port, engine: process.env.FUACTL_ENGINE, map, def: MAPS[map], outDir, cvars });
    const v = judgeFractions(pairs);
    failed += v.failed.length;
    for (const p of v.failed) lines.push(`FAIL ${map} ${p.camera} ${p.fraction}: worst ${p.worst.map((w) => `${w.d.toFixed(1)}@${w.x},${w.y}`).join(" ")}, ${p.stats ? `${p.stats.shown} tic sprites shown` : "no ticstats"}`);
    lines.push(`${v.pass ? "PASS" : "FAIL"} ${map}: ${pairs.length - v.failed.length} of ${pairs.length} pairs the same`);
    for (const k of checks) { if (!k.ok) failed++; lines.push(`${k.ok ? "PASS" : "FAIL"} ${map} ${k.name}: ${k.why}`); }
  }
  return { lines: [...lines, `frames in ${outDir}`], failed };
}

runScenarioIfMain(import.meta.url, () => ({ name: "gpusprites" }), compareGpuSprites);
