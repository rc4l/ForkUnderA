// [rc4l] A 4:3 lens pitched 39 degrees down from a pillar onto a patch wholly in the screen's bottom-right corner: exit 0 when GL and Vulkan draw the patch there as they draw it facing it (queue #58).
//
// usage:  node scenarios/frustumcorner.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch, grab } from "../src/template.mjs";
import { resolveEngine, slashed } from "../src/launch.mjs";
import { decodePNG, boxStats } from "../src/pngcore.mjs";

// [rc4l] The patch spans headings 72 to 88 degrees right of north, past the 70.2 GL's estimate gave this pitch and inside the 78.8 the corner sees.
export const PATCH = [[380, 13], [361, 117], [723, 235], [760, 27]];

export function buildFrustumCornerWad() {
  const m = new UdmfMap();
  const room = m.room(-1024, -1024, 1024, 1024, { heightfloor: 0, heightceiling: 1024, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(-32, -32, 32, 32, { heightfloor: 512, heightceiling: 1024, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 },
    room, () => ({ texturelower: "STARTAN2" }));
  m.shape(PATCH, { heightfloor: 0, heightceiling: 1024, texturefloor: "FLAT14", textureceiling: "CEIL3_5", lightlevel: 255 }, room);
  m.thing({ x: 0, y: 0, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Facing north the patch fills the bottom-right corner; turned toward it, it fills the middle, which every build draws.
const CORNER = { x: 0, y: 0, z: 512, angle: 90, pitch: 38.85 };
const FACING = { ...CORNER, angle: 10 };
const CORNER_BOX = [0.91, 0.95, 0.98, 0.99];
const MIDDLE_BOX = [0.47, 0.47, 0.53, 0.53];

export const SIDES = [{ name: "gl", gl: true, launchCvars: { vid_aspect: "3" }, cvars: { screenblocks: 12 } },
  { name: "vk", noGl: true, launchCvars: { vid_aspect: "3" }, cvars: { screenblocks: 12 } }];

// [rc4l] The corner holds the patch when its colour is the patch's own within a quarter of each channel or 12 levels.
export function judgeFrustumCorner(label, corner, patch) {
  if (!(patch.mean > 20)) return { line: `FAIL ${label}: the patch is not drawn even facing it (mean ${patch.mean.toFixed(1)})`, failed: 1 };
  const off = ["r", "g", "b"].filter((k) => Math.abs(corner[k] - patch[k]) > Math.max(12, patch[k] * 0.25));
  const rgb = (s) => ["r", "g", "b"].map((k) => s[k].toFixed(0)).join(",");
  const text = `corner rgb ${rgb(corner)} against the patch's ${rgb(patch)}`;
  return off.length ? { line: `FAIL ${label}: the walk cut the corner, ${text}`, failed: 1 } : { line: `PASS ${label}: ${text}`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  const exe = slashed(resolveEngine());
  const tag = `frustumcorner_${label.replace(/\W+/g, "_")}`;
  const side = SIDES.find((s) => label.endsWith(s.name));
  const facing = await grab(c, exe, `${tag}_facing`, FACING, side);
  const corner = await grab(c, exe, `${tag}_corner`, CORNER, side);
  if (!facing || !corner) return { line: `FAIL ${label}: a capture came back empty`, failed: 1 };
  return judgeFrustumCorner(label, boxStats(decodePNG(corner), CORNER_BOX), boxStats(decodePNG(facing), MIDDLE_BOX));
}

runScenarioIfMain(import.meta.url, () => ({ name: "frustumcorner", wad: buildFrustumCornerWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
