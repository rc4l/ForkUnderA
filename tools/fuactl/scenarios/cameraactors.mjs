// [Doruk-Aksoy] A camera texture aimed at green columns and a yellow particle fountain in a room the player never sees: exit 0 when the screen shows both, as UZDoom's does, which fails while a camera's picture draws the player's last frame's sprites and particles.
//
// usage:  node scenarios/cameraactors.mjs [port], or fuactl scenarios cameraactors
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, FREEDOOM2 } from "../src/template.mjs";

const ANIMDEFS = "cameratexture FUACAM 256 128 fit 128 64\n";
const CAMERA_TID = 5;
export const YELLOW_FOUNTAIN = 9030;
export const COLUMNS = [160, 256, 352];
// [Doruk-Aksoy] The green, yellow and red pixels a screen needs: the columns, the fountain's particles, and the camera room's floor, which shows the screen was drawn at all.
export const GREEN_PIXELS = 40;
export const YELLOW_PIXELS = 40;
export const RED_PIXELS = 400;
// [Doruk-Aksoy] The screen's share of the picture, [left, top, right, bottom], as cameratex frames it, since the side walls' panels are green too.
export const SCREEN_BOX = [0.26, 0.33, 0.74, 0.57];
export const CVARS = { i_pauseinbackground: 0, fua_render_in_background: 1, snd_sfxvolume: 0, snd_musicvolume: 0, show_messages: 0 };

// [Doruk-Aksoy] The player's room, its north wall the screen, and apart from it, so in a map section of its own, the camera's room with a red floor, a yellow particle fountain close ahead of the camera and three green columns beyond.
export function buildCameraActorsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "FUACAM" : "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  m.room(1792, 0, 2304, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5", lightlevel: 255 },
    () => ({ side: { texturemiddle: "COMPBLUE" } }));
  m.thing({ x: 1856, y: 256, height: 32, angle: 0, type: 9001, id: CAMERA_TID });
  for (const y of COLUMNS) m.thing({ x: 2200, y, angle: 180, type: THING.TallGreenColumn });
  m.thing({ x: 1940, y: 256, angle: 0, type: YELLOW_FOUNTAIN });
  // [Doruk-Aksoy] SetCameraToTexture(CAMERA_TID, "FUACAM", 90) from an OPEN script.
  const behavior = acs0([PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE, PCD.TERMINATE], ["FUACAM"]);
  return wad([{ name: "ANIMDEFS", data: ANIMDEFS }, ...mapLumps("MAP01", m, { behavior })]);
}

// [Doruk-Aksoy] The green, the particles' yellow and the red pixels inside a picture's box, by which channels stand out.
export function screenPixels(p, box = SCREEN_BOX) {
  let green = 0, yellow = 0, red = 0;
  for (let y = Math.floor(p.h * box[1]); y < Math.ceil(p.h * box[3]); y++)
    for (let x = Math.floor(p.w * box[0]); x < Math.ceil(p.w * box[2]); x++) {
      const i = (y * p.w + x) * p.channels, r = p.data[i], g = p.data[i + 1], b = p.data[i + 2];
      if (g > r + 30 && g > b + 30) green++;
      else if (r > 88 && g > 56 && b < 24 && g > r * 0.45) yellow++;
      else if (r > g + 40 && r > b + 40) red++;
    }
  return { green, yellow, red };
}

// [Doruk-Aksoy] Passes a screen showing the camera room with its columns and particles, fails one showing the room without either, and cannot judge one not showing the room.
export function judgeScreen(p) {
  const { green, yellow, red } = screenPixels(p);
  if (red < RED_PIXELS) return { lines: [`FAIL the screen shows ${red} red floor pixels, so the camera's picture is not on it`], failed: 1 };
  const checks = [["columns", green, "green", GREEN_PIXELS], ["particles", yellow, "yellow", YELLOW_PIXELS]];
  const lines = checks.map(([what, got, colour, want]) => `${got < want ? "FAIL" : "PASS"} the camera's ${what} on the screen: ${got} ${colour} pixels, ${want} wanted`);
  return { lines, failed: lines.filter((l) => l.startsWith("FAIL")).length };
}

export async function runCameraActors({ port, engine, outDir }) {
  const { launchInstance, stopInstance } = await import("../src/launch.mjs");
  const { attach } = await import("../src/session.mjs");
  const cap = await import("../src/capture.mjs");
  const { captureVulkan } = await import("../src/shot.mjs");
  const { decodePNG } = await import("../src/pngcore.mjs");
  const wadPath = path.join(outDir, "cameraactors.wad");
  fs.writeFileSync(wadPath, buildCameraActorsWad());
  const inst = await launchInstance({ port, engine, map: "MAP01", iwad: FREEDOOM2, focus: false, cvars: CVARS, extraArgs: ["-file", wadPath] });
  const c = await attach(inst);
  try {
    await cap.waitInLevel(c);
    await cap.waitTics(c, 35);
    const shot = await captureVulkan(c, path.join(outDir, "screen.png"));
    if (!shot) return { lines: ["FAIL no picture was taken"], failed: 1 };
    return judgeScreen(decodePNG(shot));
  } finally {
    c.close();
    await stopInstance(inst);
  }
}

// [Doruk-Aksoy] Run by `fuactl scenarios cameraactors` or directly, its picture kept in a temp folder named in its last line.
export async function compareCameraActors({ port = 7798 } = {}) {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "cameraactors-"));
  const r = await runCameraActors({ port, engine: process.env.FUACTL_ENGINE, outDir });
  return { lines: [...r.lines, `picture in ${outDir}`], failed: r.failed };
}

runScenarioIfMain(import.meta.url, () => ({ name: "cameraactors" }), compareCameraActors);
