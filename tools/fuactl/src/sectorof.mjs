// [rc4l] Which sector's colour a renderer's pixel follows, such as the neighbour GL's missing-texture fill borrowed, found by painting halves of the map red and green and keeping the half the pixel shows.
import path from "node:path";
import * as ui from "./ui.mjs";
import { exec, waitTics } from "./capture.mjs";
import { captureVulkan, sweepDirFor } from "./shot.mjs";
import { decodePNG, px } from "./pngcore.mjs";

export const RED = [255, 0, 0], GREEN = [0, 255, 0];

// [rc4l] The map's sector count, read off fua_sector_color's refusal of a sector it does not have; null when the echo is not that.
export function sectorCountFrom(text) {
  const m = /the map has (\d+)/.exec(String(text));
  return m ? Number(m[1]) : null;
}

// [rc4l] Console lines recolouring sectors lo..hi to rgb, per commands to a line; none when lo is past hi.
export function paintCommands(lo, hi, [r, g, b], per = 400) {
  const lines = [];
  for (let s = lo; s <= hi; s += per) {
    const cmds = [];
    for (let k = s; k <= Math.min(hi, s + per - 1); k++) cmds.push(`fua_sector_color ${k} ${r} ${g} ${b}`);
    lines.push(cmds.join(";"));
  }
  return lines;
}

// [rc4l] The paint a pixel shows, "red" or "green" when only that channel survives the light and the texel, else null: sky, a fullbright, or a texel with neither.
export function paintOf([r, g]) {
  if (r > 0 && g === 0) return "red";
  if (g > 0 && r === 0) return "green";
  return null;
}

// [rc4l] The sector of 0..count-1 whose colour the pixel follows, where look(lo, mid, hi) paints lo..mid red and the rest of lo..hi green and returns paintOf the pixel; null when it follows no sector's.
export async function bisectSector(count, look) {
  if (count <= 0 || (await look(0, count - 1, count - 1)) !== "red" || (await look(0, -1, count - 1)) !== "green") return null;
  let lo = 0, hi = count - 1;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    const paint = await look(lo, mid, hi);
    if (paint === "red") hi = mid;
    else if (paint === "green") lo = mid + 1;
    else return null;
  }
  return lo;
}

// [rc4l] bisectSector on a live instance, reading pixel (x, y) of side's capture ("gl" or "vk"); the sectors keep their paint afterwards.
export async function sectorOf(c, { side = "gl", x, y, engineBin, log = () => {} }) {
  const count = sectorCountFrom(await exec(c, "fua_sector_color -1 0 0 0"));
  if (!count) throw new Error("no sector count: this binary has no fua_sector_color");
  await c.rpc("sim.resume", {}).catch(() => {});
  let frames = 0;
  const look = async (lo, mid, hi) => {
    for (const line of [...paintCommands(lo, mid, RED), ...paintCommands(mid + 1, hi, GREEN)]) await exec(c, line, { quietMs: 60 });
    await waitTics(c, 2);
    const tag = `sectorof${frames++}`;
    const file = side === "vk" ? await captureVulkan(c, path.posix.join(sweepDirFor(engineBin), `${tag}_vk.png`))
      : (await ui.screenshot(c, engineBin, `sweep/${tag}_gl`)).path;
    const paint = file ? paintOf(px(decodePNG(file), x, y)) : null;
    log(`${lo}..${mid} red, to ${hi} green: ${paint || "neither"}`);
    return paint;
  };
  return { sector: await bisectSector(count, look), count, frames };
}
