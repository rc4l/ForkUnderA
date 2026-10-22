// [rc4l] Sprites close up on the path with a binding per material, the sprite atlas against each sprite's own binding in one paused frame: exit 0 when the two frames match and the sprites really drew from the atlas (queue #59).
//
// usage:  node scenarios/spriteatlas.mjs [port]
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { encodePNG, decodePNG, abDiff } from "../src/pngcore.mjs";
import { frozenAB } from "../src/shot.mjs";
import { THING } from "../src/doomnums.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] An all-white brightmap on the lamp, so a brightmap page the atlas got wrong shows as a dark lamp.
const WHITE = encodePNG(8, 8, Buffer.alloc(8 * 8 * 3, 255));
const GLDEFS = `brightmap sprite COLUA0 { map BMWHITE }
`;

// [rc4l] A dim room with sprites of several sizes overlapping in depth in front of the player, so the sorted pass interleaves them; MAP02 keeps some and brings others, since the atlas keeps its images from one level to the next.
function spriteRoom(row) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 96 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 48, angle: 90, type: 1 });
  row.forEach((type, i) => m.thing({ x: 150 + i * 36, y: 150 + (i % 3) * 30, angle: 270, type }));
  m.thing({ x: 240, y: 120, angle: 270, type: THING.InvulnerabilitySphere });
  return m;
}

export function buildSpriteAtlasWad() {
  const first = spriteRoom([THING.Column, THING.BurningBarrel, THING.TechLamp, THING.HeartColumn, THING.Medikit, THING.RocketBox, THING.TechPillar]);
  const second = spriteRoom([THING.Berserk, THING.Column, THING.RadSuit, THING.EvilEye, THING.Infrared, THING.RocketLauncher, THING.BurningBarrel]);
  return wad([{ name: "TX_START", data: "" }, { name: "BMWHITE", data: WHITE }, { name: "TX_END", data: "" },
    { name: "GLDEFS", data: GLDEFS }, ...mapLumps("MAP01", first), ...mapLumps("MAP02", second)]);
}

const AT = { x: 256, y: 48, z: 0, angle: 90, pitch: 0 };
export const SIDES = [{ name: "per-material", noGl: true, launchCvars: { fua_dg_bindless: "0" } }];
const ATLAS_DRAWS = /sprite atlas on: (\d+) draws from it/;

// [rc4l] The pair's verdict: the sprites must have drawn from the atlas, and switching it may change no more than repeating a frame does.
export function judgeSpriteAtlas(label, { atlasDraws, changed, noise }) {
  if (!(atlasDraws > 0)) return { line: `FAIL ${label}: no sprite drew from the atlas`, failed: 1 };
  if (!(changed <= Math.max(noise, 0.01))) return { line: `FAIL ${label}: the atlas changed the frame by ${changed.toFixed(4)} against a noise of ${noise.toFixed(4)}`, failed: 1 };
  return { line: `PASS ${label}: ${atlasDraws} sprite draws from the atlas, the frame ${changed.toFixed(4)} apart against ${noise.toFixed(4)} of noise`, failed: 0 };
}

// [rc4l] One level's pair: the atlas's draws counted, then the frame with it off, on and off again.
async function pairOn(c, label) {
  await c.rpc("player.setpos", { ...AT });
  await cap.waitTics(c, 35);
  const m = ATLAS_DRAWS.exec(await cap.exec(c, "fua_dg_dynstats", { quietMs: 400 }));
  const files = await frozenAB(c, { cvar: "fua_dg_spriteatlas", values: ["0", "1"], at: AT, outDir: path.join(os.tmpdir(), "fuactl-spriteatlas"), tag: `spriteatlas_${label.replace(/\W+/g, "_")}` });
  if (files.some((f) => !f)) return { line: `FAIL ${label}: a capture came back empty`, failed: 1 };
  const [A, B, A2] = files.map((f) => decodePNG(f));
  return judgeSpriteAtlas(label, { atlasDraws: m ? Number(m[1]) : 0, ...abDiff(A, B, A2, [0, 0.05, 1, 0.75]) });
}

export async function judgeLaunch(c, inst, label) {
  await cap.exec(c, "fua_dg_spriteatlas 1");
  const first = await pairOn(c, label);
  await cap.exec(c, "map MAP02", { quietMs: 400 });
  await cap.waitInLevel(c);
  const second = await pairOn(c, `${label} then MAP02`);
  return { line: `${first.line}\n${second.line}`, failed: first.failed + second.failed };
}

runScenarioIfMain(import.meta.url, () => ({ name: "spriteatlas", wad: buildSpriteAtlasWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
