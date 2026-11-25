// [Doruk-Aksoy] The player spins a quarter turn a tic inside a ring of green columns that looks the same every quarter, one frame a tic: exit 0 when the spinning picture shows as many columns as the still one, which fails while the actor sweep culls each frame with the one before's view.
//
// usage:  node scenarios/sweepcull.mjs [port], or fuactl scenarios sweepcull
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, FREEDOOM2 } from "../src/template.mjs";

export const RING = 64;
export const RADIUS = 1024;
// [Doruk-Aksoy] The rows the columns stand in, as shares of the picture's height, and the green pixels a picture column needs there to hold one.
export const COLUMN_BAND = [0.40, 0.58];
export const GREEN_PIXELS = 3;
// [Doruk-Aksoy] One frame a tic (cl_capfps), so each quarter turn reaches the screen whole in the frame after the one before it.
export const CVARS = { vid_maxfps: 0, vid_vsync: 0, i_pauseinbackground: 0, fua_render_in_background: 1, snd_sfxvolume: 0, snd_musicvolume: 0, show_messages: 0, cl_capfps: 1 };
// [Doruk-Aksoy] A quarter turn a tic at every turn speed, in angleturn's 65,536ths of a circle.
const QUARTER_TURN = 16384;

// [Doruk-Aksoy] One square room, the player at its centre facing east, and RING green columns, a sprite of one frame and a colour nothing else in the room has, on a circle offset half a step so every quarter heading sees the same ring.
export function buildSweepCullWad() {
  const m = new UdmfMap();
  m.room(-2048, -2048, 2048, 2048, { heightfloor: 0, heightceiling: 512, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 0, y: 0, angle: 0, type: 1 });
  for (let i = 0; i < RING; i++) {
    const a = (2 * Math.PI * (i + 0.5)) / RING;
    m.thing({ x: RADIUS * Math.cos(a), y: RADIUS * Math.sin(a), angle: 0, type: THING.TallGreenColumn });
  }
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] The green columns a picture shows: runs of picture columns with GREEN_PIXELS green pixels in COLUMN_BAND, a gap under GAP_PIXELS wide joining one column's stripes.
export const GAP_PIXELS = 6;
export function columnsShown(p, band = COLUMN_BAND) {
  const y0 = Math.floor(p.h * band[0]), y1 = Math.ceil(p.h * band[1]);
  let runs = 0, lastHit = -Infinity;
  for (let x = 0; x < p.w; x++) {
    let n = 0;
    for (let y = y0; y < y1; y++) {
      const i = (y * p.w + x) * p.channels, r = p.data[i], g = p.data[i + 1], b = p.data[i + 2];
      if (g > r + 30 && g > b + 30) n++;
    }
    if (n < GREEN_PIXELS) continue;
    if (x - lastHit > GAP_PIXELS) runs++;
    lastHit = x;
  }
  return runs;
}

// [Doruk-Aksoy] The spinning picture's columns against the still one's, which the ring's symmetry makes the same.
export function judgeSweep(still, spinning) {
  const want = columnsShown(still), got = columnsShown(spinning);
  if (want < 8) return { lines: [`FAIL the still view shows ${want} columns, too few to judge`], failed: 1 };
  const bad = got < want;
  return { lines: [`${bad ? "FAIL" : "PASS"} spinning a quarter a frame: ${got} of the still view's ${want} columns drawn`], failed: bad ? 1 : 0 };
}

export async function runSweepCull({ port, engine, outDir }) {
  const { launchInstance, stopInstance } = await import("../src/launch.mjs");
  const { attach } = await import("../src/session.mjs");
  const cap = await import("../src/capture.mjs");
  const { captureVulkan } = await import("../src/shot.mjs");
  const { decodePNG } = await import("../src/pngcore.mjs");
  const wadPath = path.join(outDir, "sweepcull.wad");
  fs.writeFileSync(wadPath, buildSweepCullWad());
  const inst = await launchInstance({ port, engine, map: "MAP01", iwad: FREEDOOM2, focus: false, cvars: CVARS, extraArgs: ["-file", wadPath] });
  const c = await attach(inst);
  try {
    await cap.waitInLevel(c);
    await cap.waitTics(c, 35);
    const still = await captureVulkan(c, path.join(outDir, "still.png"));
    await cap.exec(c, `turnspeeds ${QUARTER_TURN} ${QUARTER_TURN} ${QUARTER_TURN} ${QUARTER_TURN}`, { quietMs: 100 });
    await cap.exec(c, "+right", { quietMs: 60 });
    await cap.waitTics(c, 20);
    const spinning = await captureVulkan(c, path.join(outDir, "spinning.png"));
    await cap.exec(c, "-right", { quietMs: 60 });
    if (!still || !spinning) return { lines: ["FAIL no picture was taken"], failed: 1 };
    return judgeSweep(decodePNG(still), decodePNG(spinning));
  } finally {
    c.close();
    await stopInstance(inst);
  }
}

// [Doruk-Aksoy] Run by `fuactl scenarios sweepcull` or directly, its pictures kept in a temp folder named in its last line.
export async function compareSweepCull({ port = 7797 } = {}) {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "sweepcull-"));
  const r = await runSweepCull({ port, engine: process.env.FUACTL_ENGINE, outDir });
  return { lines: [...r.lines, `pictures in ${outDir}`], failed: r.failed };
}

runScenarioIfMain(import.meta.url, () => ({ name: "sweepcull" }), compareSweepCull);
