// [rc4l] Template-map regression harness: GL against Vulkan on a generated wad, at fixed vantages, judged by box -- see the template-maps skill.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { BridgeClient } from "./client.mjs";
import * as cap from "./capture.mjs";
import * as shot from "./shot.mjs";
import * as ui from "./ui.mjs";
import { play, stopInstance } from "./play.mjs";
import { resolveEngine, engineProcessCount } from "./launch.mjs";
import { decodePNG, boxDiff } from "./pngcore.mjs";

export const FREEDOOM2 = "F:/ForkUnderA/tools/freedoom/freedoom2.wad";

// [rc4l] Mean |d| per box for one map, given each vantage's GL and Vulkan frames, as PASS/FAIL lines and a failure count.
export function judge(map, frames, boxes, size = [1280, 720]) {
  const lines = [];
  let failed = 0;
  for (const [vantage, f] of Object.entries(frames)) {
    if (!f.gl || !f.vk) { lines.push(`FAIL ${map} ${vantage}: capture failed`); failed++; continue; }
    if (f.gl.w !== size[0] || f.gl.h !== size[1] || f.vk.w !== f.gl.w || f.vk.h !== f.gl.h) {
      lines.push(`FAIL ${map} ${vantage}: frame is ${f.gl.w}x${f.gl.h} / ${f.vk.w}x${f.vk.h}, boxes assume ${size[0]}x${size[1]}`);
      failed++;
      continue;
    }
    for (const [name, { box, ceiling }] of Object.entries(boxes)) {
      const d = boxDiff(f.gl, f.vk, box);
      const ok = d <= ceiling;
      if (!ok) failed++;
      lines.push(`${ok ? "PASS" : "FAIL"} ${map} ${vantage.padEnd(8)} ${name.padEnd(8)} mean|d| ${d.toFixed(2).padStart(6)}  ceiling ${ceiling}`);
    }
  }
  return { lines, failed };
}

// [rc4l] One locked launch of the wad, handed to `body` and always torn down; a crash comes back as the error.
async function withEngine({ port, map, iwad, wadPath, gl, size }, body) {
  let inst = null;
  const c = new BridgeClient();
  try {
    inst = await play({ port, map, iwad, file: wadPath, lock: true, gl, width: size[0], height: size[1] });
    await c.connect(inst.port, { token: inst.token });
    await c.waitHello();
    await cap.exec(c, "r_drawplayersprites 0");
    // [rc4l] GL keeps walking the scene, or nothing feeds the passes that still take from it.
    if (!gl) await cap.exec(c, "fua_dg_standalone 0");
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

// [rc4l] Stand at a vantage and take one renderer's frame; GL comes from a Vulkan-off launch, the one reliable GL reference.
async function grab(c, exe, tag, at, gl) {
  await c.rpc("sim.resume", {}).catch(() => {});
  await c.rpc("player.setpos", at);
  await cap.waitTics(c, 15);
  if (gl) return (await ui.screenshot(c, exe, `sweep/${tag}`)).path;
  return (await shot.shotPair(c, tag, { engineBin: exe })).vk;
}

// [rc4l] Build the wad, capture every map at every vantage in both renderers, and judge; returns { lines, failed }.
export async function compareTemplate({ name, wad, maps, vantages, boxes, iwad = FREEDOOM2, port = 7796, size = [1280, 720] }) {
  // [rc4l] GL reads its frame back out of the window, so another engine on top can make it garbage.
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
      for (const gl of [true, false]) {
        await withEngine({ port, map, iwad, wadPath, gl, size }, async (c) => {
          for (const v of vantages) {
            const file = await grab(c, exe, `${name}_${map}_${v.name}_${gl ? "gl" : "vk"}`, v.at, gl);
            (frames[v.name] ||= {})[gl ? "gl" : "vk"] = file ? decodePNG(file) : null;
          }
        });
      }
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
