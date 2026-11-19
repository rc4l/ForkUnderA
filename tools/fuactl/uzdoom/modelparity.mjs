// [Doruk-Aksoy] Model parity with UZDoom's OpenGL: an MD3 (MAP01) and an MD2 (MAP02), each as keyframe 0, keyframe 1 and an actor blending between them,
// time frozen 17 tics in by an ENTER script so both engines hold the same blend; each block's shape and lean, and the blend the animated one shows, against UZDoom's.
// usage: node uzdoom/modelparity.mjs [--port 7874] [--only MAP01]   (FUACTL_ENGINE picks our binary; needs src/quakemodel.mjs, from PR #425 on)
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { uzdoomShot, oursShot } from "./shots.mjs";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG, decodePNG, px } from "../src/pngcore.mjs";
import { acsScripts, PCD, SCRIPT } from "../src/acs.mjs";

const args = process.argv.slice(2);
const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : null);
const port = Number(flag("--port")) || 7874;
// [Doruk-Aksoy] The MD2 and MD3 writers arrive with the GPU model path (PR #425); without them this harness has nothing to build.
const { md2, md3 } = await import("../src/quakemodel.mjs").catch(() => {
  console.error("modelparity needs src/quakemodel.mjs, which comes with the GPU model path (PR #425)");
  process.exit(2);
});
const out = fs.mkdtempSync(path.join(os.tmpdir(), "modelparity-"));

// [Doruk-Aksoy] A block 16 wide and 32 tall, and the same block with its top 24 to its side and raised to 48: at blend t it is 32 + 16t tall.
const BOTTOM = [[0, 0, 0], [16, 0, 0], [16, 16, 0], [0, 16, 0]];
const FRAMES = [
  [...BOTTOM, ...BOTTOM.map(([x, y]) => [x, y, 32])],
  [...BOTTOM, ...BOTTOM.map(([x, y]) => [x, y + 24, 48])],
];
const TRIS = [[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5], [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]];
const UVS = FRAMES[0].map(([x, y, z]) => [(x + y) / 32, z / 32]);
const FREEZE_TIC = 17;

function skin() {
  const rgb = Buffer.alloc(32 * 32 * 3);
  for (let y = 0; y < 32; y++)
    for (let x = 0; x < 32; x++) {
      const at = (y * 32 + x) * 3, odd = ((x >> 3) + (y >> 3)) & 1;
      rgb[at] = 230; rgb[at + 1] = odd ? 210 : 40; rgb[at + 2] = 20;
    }
  return encodePNG(32, 32, rgb);
}

// [Doruk-Aksoy] Per format: keyframe 0 still, keyframe 1 still, and both a second each in turn, left to right.
const KINDS = [
  { suffix: "Kf0", states: "BAR1 A -1", frames: "  FrameIndex BAR1 A 0 0\n" },
  { suffix: "Kf1", states: "BAR1 A -1", frames: "  FrameIndex BAR1 A 0 1\n" },
  // [Doruk-Aksoy] A one-tic first state, since a map thing's first state starts at a random tic (P_SpawnMapThing) the two engines' RNGs need not agree on.
  { suffix: "Anim", states: "BAR1 A 1\n  Cycle:\n    BAR1 A 35\n    BAR1 B 35\n    Loop", frames: "  FrameIndex BAR1 A 0 0\n  FrameIndex BAR1 B 0 1\n" },
];
const FORMATS = [{ map: "MAP01", file: "anim.md3", tag: "Md3", num: 20230 }, { map: "MAP02", file: "anim.md2", tag: "Md2", num: 20240 }];
const XS = [312, 512, 712];

function buildPk3() {
  const decorate = [], modeldef = [], files = [];
  for (const f of FORMATS) {
    const m = new UdmfMap();
    m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "ZXGRAY", textureceiling: "ZXGRAY", lightlevel: 255 },
      () => ({ side: { texturemiddle: "ZXGRAY" } }));
    KINDS.forEach((k, i) => {
      const name = `ZX${f.tag}${k.suffix}`, num = f.num + i;
      decorate.push(`Actor ${name} ${num}\n{\n  Radius 16\n  Height 32\n  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ${k.states}\n    Stop\n  }\n}\n`);
      modeldef.push(`Model ${name}\n{\n  Path "models/"\n  Model 0 "${f.file}"\n  Skin 0 "zxskin.png"\n  Scale 3.0 3.0 3.0\n${k.frames}}\n`);
      m.thing({ x: XS[i], y: 600, angle: 90, type: num });
    });
    m.thing({ x: 512, y: 150, angle: 90, type: 1 });
    // [Doruk-Aksoy] ENTER: wait, then give the player a time freezer, which stops every other actor on the same tic in both engines, and both read no tic fraction while frozen.
    const behavior = acsScripts([{ type: SCRIPT.ENTER, number: 1, code: [PCD.DELAYDIRECT, FREEZE_TIC, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.GIVEINVENTORY, PCD.TERMINATE] }],
      ["PowerTimeFreezer"]);
    files.push({ name: `maps/${f.map}.wad`, data: wad(mapLumps(f.map, m, { behavior })) });
  }
  files.push(
    { name: "decorate.txt", data: decorate.join("\n") },
    { name: "modeldef.txt", data: modeldef.join("\n") },
    { name: "models/anim.md3", data: md3({ frames: FRAMES, tris: TRIS, uvs: UVS }) },
    { name: "models/anim.md2", data: md2({ frames: FRAMES, tris: TRIS, uvs: UVS.map(([u, v]) => [Math.round(u * 32), Math.round(v * 32)]), skinW: 32, skinH: 32 }) },
    { name: "models/zxskin.png", data: skin() },
    { name: "textures/ZXGRAY.png", data: encodePNG(64, 64, Buffer.alloc(64 * 64 * 3, 128)) },
  );
  const file = path.join(out, "modelparity.pk3");
  fs.writeFileSync(file, pk3(files));
  return file;
}

// [Doruk-Aksoy] The skin's red and yellow, which the grey room has none of.
const isModel = ([r, , b]) => r > 120 && b < 100 && r - b > 80;

// [Doruk-Aksoy] Between the top eighth (our PAUSE banner and console lines) and the status bar (the bottom fifth), both red and yellow too, the column runs holding the skin's colours, at least `gap` columns apart: the blocks, left to right.
function blockSpans(img, gap = 8) {
  const spans = [];
  let start = -1, last = -1;
  for (let x = 0; x < img.w; x++) {
    let hit = false;
    for (let y = Math.floor(img.h * 0.125); y < img.h * 0.8 && !hit; y++) hit = isModel(px(img, x, y));
    if (!hit) continue;
    if (start < 0 || x - last > gap) { if (start >= 0) spans.push([start, last + 1]); start = x; }
    last = x;
  }
  if (start >= 0) spans.push([start, last + 1]);
  return spans;
}

// [Doruk-Aksoy] The block over columns x0..x1: its height in pixels, its width over its height, and its lean (its top band's middle less its bottom band's, over its height).
function block(img, [x0, x1]) {
  const rows = new Map();
  for (let y = Math.floor(img.h * 0.125); y < img.h * 0.8; y++)
    for (let x = x0; x < x1; x++) if (isModel(px(img, x, y))) { if (!rows.has(y)) rows.set(y, []); rows.get(y).push(x); }
  if (rows.size === 0) return null;
  const ys = [...rows.keys()].sort((a, b) => a - b);
  const top = ys[0], bottom = ys[ys.length - 1], h = bottom - top + 1;
  let left = Infinity, right = -Infinity;
  for (const xs of rows.values()) { left = Math.min(left, ...xs); right = Math.max(right, ...xs); }
  const band = Math.max(1, Math.round(h * 0.1));
  const mid = (from, to) => { const xs = ys.filter((y) => y >= from && y <= to).flatMap((y) => rows.get(y)); return xs.reduce((s, x) => s + x, 0) / xs.length; };
  return { h, aspect: (right - left + 1) / h, lean: (mid(top, top + band) - mid(bottom - band, bottom)) / h };
}

function measure(png) {
  const img = decodePNG(png);
  const spans = blockSpans(img);
  if (spans.length !== 3) throw new Error(`${spans.length} blocks in ${png}, not 3: ${JSON.stringify(spans)}`);
  const [kf0, kf1, anim] = spans.map((s) => block(img, s));
  // [Doruk-Aksoy] The blend the animated block shows, from its height against keyframe 0's at the same depth: 32 + 16t over 32.
  return { kf0, kf1, anim, t: 2 * (anim.h / kf0.h - 1) };
}

const pk3File = buildPk3();
let ok = true;
const row = (label, uz, ours, tol) => {
  const match = Math.abs(ours - uz) <= tol;
  ok &&= match;
  console.log(`${label.padEnd(26)} ${uz.toFixed(3).padStart(7)} ${ours.toFixed(3).padStart(7)}  ${match ? "ok" : "MISMATCH"}`);
};
for (const f of FORMATS.filter((x) => !flag("--only") || x.map === flag("--only"))) {
  const uz = measure(uzdoomShot(pk3File, f.map, out));
  const ours = measure(await oursShot(pk3File, f.map, { port, minTic: 70, tag: "modelparity" }));
  console.log(`\n${f.map} ${f.file}: keyframe 0, keyframe 1, and frozen ${FREEZE_TIC} tics into a 35-tic blend`);
  console.log(`${"".padEnd(26)}  UZDoom    ours`);
  for (const k of ["kf0", "kf1", "anim"]) {
    row(`${k} width / height`, uz[k].aspect, ours[k].aspect, 0.03);
    row(`${k} lean`, uz[k].lean, ours[k].lean, 0.03);
  }
  row("anim blend (from height)", uz.t, ours.t, 0.06);
  row("kf1 height / kf0 height", uz.kf1.h / uz.kf0.h, ours.kf1.h / ours.kf0.h, 0.03);
}
console.log(`\nshots in ${out}`);
process.exit(ok ? 0 : 1);
