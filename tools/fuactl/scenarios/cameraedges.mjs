// [Doruk-Aksoy] Three camera screens in one frame, as UZDoom draws them: two rooms the player never sees (columns and a decal; particles and a monster) and the player seen by a camera in its own room; exit 0 when each screen shows its own room's things and none of another's (room B's mirror is not judged, since a camera view draws no mirror yet).
//
// usage:  node scenarios/cameraedges.mjs [port], or fuactl scenarios cameraedges
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, FREEDOOM2 } from "../src/template.mjs";

// [Doruk-Aksoy] Each screen's texture fitted to its wall, so the picture shows once across and once down.
const ANIMDEFS = ["CAMA", "CAMB", "CAMC"].map((t) => `cameratexture ${t} 256 128 fit 512 128\n`).join("");
// [Doruk-Aksoy] A big magenta mark, a colour nothing else on the map has, at the number the Decal thing (9200) asks for.
export const DECAL_ID = 4001;
const DECALDEF = `decal FuaCameraMark ${DECAL_ID}\n{\n\tpic SCORCH1\n\tshade "FF 00 FF"\n\tx-scale 2.0\n\ty-scale 2.0\n}\n`;
export const YELLOW_FOUNTAIN = 9030, DECAL_THING = 9200, ZOMBIEMAN = 3004, LINE_MIRROR = 182;
export const CAMERAS = { A: 5, B: 6, C: 7 };
// [Doruk-Aksoy] The north wall's three screens, west to east, each 512 wide.
export const SCREENS = ["A", "B", "C"];
export const CVARS = { i_pauseinbackground: 0, fua_render_in_background: 1, snd_sfxvolume: 0, snd_musicvolume: 0, show_messages: 0 };

// [Doruk-Aksoy] The player's room, its north wall three screens; room A (columns, a decal on its far wall) and room B (a fountain, a dormant zombieman facing away, a mirror for a far wall), apart from it; camera C in the player's room, looking back at the player.
export function buildCameraEdgesWad() {
  const m = new UdmfMap();
  const room = m.sector({ heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 });
  const plain = { texturemiddle: "STARTAN2" };
  m.wall(0, 0, 0, 1024, room, plain);
  SCREENS.forEach((s, i) => m.wall(i * 512, 1024, (i + 1) * 512, 1024, room, { texturemiddle: `CAM${s}` }));
  m.wall(1536, 1024, 1536, 0, room, plain);
  m.wall(1536, 0, 0, 0, room, plain);
  m.thing({ x: 768, y: 128, angle: 90, type: 1 });
  m.thing({ x: 768, y: 400, height: 40, angle: 270, type: 9001, id: CAMERAS.C });

  m.room(3072, 0, 3584, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5", lightlevel: 255 },
    () => ({ side: { texturemiddle: "COMPBLUE" } }));
  m.thing({ x: 3136, y: 256, height: 32, angle: 0, type: 9001, id: CAMERAS.A });
  for (const y of [176, 256, 336]) m.thing({ x: 3400, y, angle: 180, type: THING.TallGreenColumn });
  m.thing({ x: 3560, y: 420, height: 48, angle: 180, type: DECAL_THING, arg0: DECAL_ID & 255, arg1: DECAL_ID >> 8 });

  m.room(4608, 0, 5120, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5", lightlevel: 255 },
    (edge) => edge === "east" ? { line: { special: LINE_MIRROR }, side: { texturemiddle: "COMPBLUE" } } : { side: { texturemiddle: "COMPBLUE" } });
  m.thing({ x: 4672, y: 256, height: 32, angle: 0, type: 9001, id: CAMERAS.B });
  m.thing({ x: 4780, y: 200, angle: 0, type: YELLOW_FOUNTAIN });
  m.thing({ x: 4900, y: 320, angle: 0, type: ZOMBIEMAN, dormant: true });

  // [Doruk-Aksoy] SetCameraToTexture for each screen from an OPEN script.
  const code = [];
  SCREENS.forEach((s, i) => code.push(PCD.PUSHNUMBER, CAMERAS[s], PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE));
  const behavior = acs0([...code, PCD.TERMINATE], SCREENS.map((s) => `CAM${s}`));
  return wad([{ name: "ANIMDEFS", data: ANIMDEFS }, { name: "DECALDEF", data: DECALDEF }, ...mapLumps("MAP01", m, { behavior })]);
}

// [Doruk-Aksoy] The colours a screen is judged by: the columns' green, the particles' yellow, the decal's magenta.
export function classify(r, g, b) {
  if (g > r + 30 && g > b + 30) return "green";
  if (r > 88 && g > 56 && b < 24 && g > r * 0.45) return "yellow";
  if (r > 120 && b > 120 && g < Math.min(r, b) * 0.55) return "magenta";
  return null;
}
export function boxPixels(p, box) {
  const n = { green: 0, yellow: 0, magenta: 0 };
  for (let y = Math.floor(p.h * box[1]); y < Math.ceil(p.h * box[3]); y++)
    for (let x = Math.floor(p.w * box[0]); x < Math.ceil(p.w * box[2]); x++) {
      const i = (y * p.w + x) * p.channels, c = classify(p.data[i], p.data[i + 1], p.data[i + 2]);
      if (c) n[c]++;
    }
  return n;
}

// [Doruk-Aksoy] Each screen's box in a picture from the player's start, [left, top, right, bottom], and the colours it must and must not show.
export const SCREEN_BOXES = { A: [0.15, 0.40, 0.375, 0.53], B: [0.387, 0.40, 0.613, 0.53], C: [0.625, 0.40, 0.85, 0.53] };
export const WANT = { A: { green: 40, magenta: 40 }, B: { yellow: 20 }, C: { green: 5 } };
export const NONE = { A: ["yellow"], B: ["green", "magenta"], C: ["yellow", "magenta"] };

export function judgeScreens(p, boxes = SCREEN_BOXES) {
  const lines = [];
  for (const s of SCREENS) {
    const n = boxPixels(p, boxes[s]);
    for (const [colour, want] of Object.entries(WANT[s] || {}))
      lines.push(`${n[colour] < want ? "FAIL" : "PASS"} screen ${s} shows its room's ${colour}: ${n[colour]} pixels, ${want} wanted`);
    for (const colour of NONE[s])
      lines.push(`${n[colour] > 4 ? "FAIL" : "PASS"} screen ${s} shows no other room's ${colour}: ${n[colour]} pixels`);
  }
  return { lines, failed: lines.filter((l) => l.startsWith("FAIL")).length };
}

export async function runCameraEdges({ port, engine, outDir }) {
  const { launchInstance, stopInstance } = await import("../src/launch.mjs");
  const { attach } = await import("../src/session.mjs");
  const cap = await import("../src/capture.mjs");
  const { captureVulkan } = await import("../src/shot.mjs");
  const { decodePNG } = await import("../src/pngcore.mjs");
  const wadPath = path.join(outDir, "cameraedges.wad");
  fs.writeFileSync(wadPath, buildCameraEdgesWad());
  const inst = await launchInstance({ port, engine, map: "MAP01", iwad: FREEDOOM2, focus: false, cvars: CVARS, extraArgs: ["-file", wadPath] });
  const c = await attach(inst);
  try {
    await cap.waitInLevel(c);
    await cap.waitTics(c, 35);
    const shot = await captureVulkan(c, path.join(outDir, "screens.png"));
    if (!shot) return { lines: ["FAIL no picture was taken"], failed: 1 };
    return judgeScreens(decodePNG(shot));
  } finally {
    c.close();
    await stopInstance(inst);
  }
}

// [Doruk-Aksoy] Run by `fuactl scenarios cameraedges` or directly, its picture kept in a temp folder named in its last line.
export async function compareCameraEdges({ port = 7799 } = {}) {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "cameraedges-"));
  const r = await runCameraEdges({ port, engine: process.env.FUACTL_ENGINE, outDir });
  return { lines: [...r.lines, `picture in ${outDir}`], failed: r.failed };
}

runScenarioIfMain(import.meta.url, () => ({ name: "cameraedges" }), compareCameraEdges);
