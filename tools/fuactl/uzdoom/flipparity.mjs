// [Doruk-Aksoy] Sprite flip parity with UZDoom: a four-colour picture plain, XFLIP, YFLIP and both, and an eight-rotation actor plain, SPRITEFLIP and SPRITEFLIP with XFLIP,
// each read by the colour in each quarter, which must be UZDoom's on every API asked for.
// usage: node uzdoom/flipparity.mjs [--port 7874] [--api vulkan|opengl|both] [--uzdoom-backend gl|vulkan]   (FUACTL_ENGINE picks our binary)
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG, decodePNG, px } from "../src/pngcore.mjs";

const W = 48, H = 64;
// [Doruk-Aksoy] The colours the pictures use, each far from the others and from the grey room.
export const COLOURS = { R: [255, 0, 0], G: [0, 255, 0], B: [0, 0, 255], Y: [255, 255, 0], C: [0, 255, 255], M: [255, 0, 255], O: [255, 128, 0], W: [255, 255, 255] };
// [Doruk-Aksoy] The rotation pictures' colours, rotation 1 to 8; each fills its left half beside a white right half, so a mirror shows as white on the left.
export const ROTATION_COLOURS = ["R", "G", "B", "Y", "C", "M", "O", "R"];

// [Doruk-Aksoy] A W by H picture whose pixel (x, y) is colourAt(x, y), its offsets at the bottom middle.
function picture(colourAt) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) rgb.set(COLOURS[colourAt(x, y)], (y * W + x) * 3);
  return encodePNG(W, H, rgb, { grab: [W / 2, H] });
}

// [Doruk-Aksoy] Red top left, green top right, blue bottom left, yellow bottom right.
export const quadPicture = () => picture((x, y) => (y < H / 2 ? (x < W / 2 ? "R" : "G") : (x < W / 2 ? "B" : "Y")));
const rotationPicture = (i) => picture((x) => (x < W / 2 ? ROTATION_COLOURS[i] : "W"));

// [Doruk-Aksoy] The actors left to right as the player sees them: two quarters pictures, the rotating one facing east (seen side on) three times, two more quarters pictures.
export const ACTORS = [
  { cls: "ZxQuadPlain", sprite: "ZXQD", flags: [] },
  { cls: "ZxQuadX", sprite: "ZXQD", flags: ["XFLIP"] },
  { cls: "ZxRotPlain", sprite: "ZXRT", flags: [] },
  { cls: "ZxRotSpriteFlip", sprite: "ZXRT", flags: ["SPRITEFLIP"] },
  { cls: "ZxRotSpriteFlipX", sprite: "ZXRT", flags: ["SPRITEFLIP", "XFLIP"] },
  { cls: "ZxQuadY", sprite: "ZXQD", flags: ["YFLIP"] },
  { cls: "ZxQuadXY", sprite: "ZXQD", flags: ["XFLIP", "YFLIP"] },
];
// [Doruk-Aksoy] The player at (PLAYER_X, 64) looks north at them 636 units off; the rotating ones stand within 64 units of the line of sight, so each is seen well inside one rotation.
const PLAYER_X = 576, ROW_Y = 700;
const XS = [304, 400, 512, 576, 640, 752, 848];

export function buildFlipPk3() {
  const decorate = ACTORS.map((a, i) => [`actor ${a.cls} ${20250 + i}`, "{", "\tRadius 16", "\tHeight 64", "\t+NOGRAVITY", "\t+NOINTERACTION",
    ...a.flags.map((f) => `\t+${f}`), "\tStates", "\t{", "\tSpawn:", `\t\t${a.sprite} A -1`, "\t\tStop", "\t}", "}"].join("\n")).join("\n") + "\n";
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "ZXGRAY", textureceiling: "ZXGRAY", lightlevel: 255 },
    () => ({ side: { texturemiddle: "ZXGRAY" } }));
  ACTORS.forEach((a, i) => m.thing({ x: XS[i], y: ROW_Y, angle: 0, type: 20250 + i }));
  m.thing({ x: PLAYER_X, y: 64, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: decorate },
    { name: "sprites/ZXQDA0.png", data: quadPicture() },
    ...ROTATION_COLOURS.map((_, i) => ({ name: `sprites/ZXRTA${i + 1}.png`, data: rotationPicture(i) })),
    { name: "textures/ZXGRAY.png", data: encodePNG(64, 64, Buffer.alloc(64 * 64 * 3, 128)) },
  ]);
}

// [Doruk-Aksoy] The picture colour a pixel is nearest, or null for the grey room and anything else not close to one.
export function colourOf([r, g, b]) {
  let best = null, bestD = 90 * 90 * 3;
  for (const [name, c] of Object.entries(COLOURS)) {
    const d = (r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2;
    if (d < bestD) { best = name; bestD = d; }
  }
  return best;
}

// [Doruk-Aksoy] Between the top eighth and the status bar, the column runs holding picture colours at least `gap` columns apart: the sprites, left to right.
export function spriteSpans(img, gap = 8) {
  const spans = [];
  let start = -1, last = -1;
  for (let x = 0; x < img.w; x++) {
    let hit = false;
    for (let y = Math.floor(img.h * 0.125); y < img.h * 0.8 && !hit; y++) hit = colourOf(px(img, x, y)) !== null;
    if (!hit) continue;
    if (start < 0 || x - last > gap) { if (start >= 0) spans.push([start, last + 1]); start = x; }
    last = x;
  }
  if (start >= 0) spans.push([start, last + 1]);
  return spans;
}

// [Doruk-Aksoy] The sprite over columns x0..x1 read as its four quarters' colours, top left, top right, bottom left, bottom right.
export function quarters(img, [x0, x1]) {
  let top = Infinity, bottom = -Infinity;
  for (let y = Math.floor(img.h * 0.125); y < img.h * 0.8; y++)
    for (let x = x0; x < x1; x++) if (colourOf(px(img, x, y)) !== null) { top = Math.min(top, y); bottom = Math.max(bottom, y); }
  const at = (fx, fy) => colourOf(px(img, Math.round(x0 + (x1 - 1 - x0) * fx), Math.round(top + (bottom - top) * fy))) || "-";
  return `${at(0.25, 0.25)}${at(0.75, 0.25)}${at(0.25, 0.75)}${at(0.75, 0.75)}`;
}

// [Doruk-Aksoy] Every sprite's quarters in a picture, or why it cannot be read.
export function readSprites(img) {
  const spans = spriteSpans(img);
  if (spans.length !== ACTORS.length) return { fault: `${spans.length} sprites, not ${ACTORS.length}: ${JSON.stringify(spans)}` };
  return { quarters: spans.map((s) => quarters(img, s)) };
}

// [Doruk-Aksoy] Each actor whose quarters differ from UZDoom's.
export function judgeFlips(uz, ours) {
  return ACTORS.flatMap((a, i) => (uz[i] === ours[i] ? [] : [`${a.cls} reads ${ours[i]}, UZDoom ${uz[i]}`]));
}

async function main(args) {
  const { uzdoomShot, oursShot } = await import("./shots.mjs");
  const { parityApis, parityUzdoomBackend } = await import("../src/uzdoom.mjs");
  const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : null);
  const port = Number(flag("--port")) || 7874;
  const out = fs.mkdtempSync(path.join(os.tmpdir(), "flipparity-"));
  const file = path.join(out, "flipparity.pk3");
  fs.writeFileSync(file, buildFlipPk3());
  // [Doruk-Aksoy] UZDoom's OpenGL now and then shows the room without its sprites (one run in three), so a picture it cannot read is taken again.
  let uz = null, uzShot = null;
  for (let i = 1; i <= 3 && !(uz && !uz.fault); i++) {
    uzShot = uzdoomShot(file, "MAP01", path.join(out, `try${i}`), parityUzdoomBackend(args));
    uz = readSprites(decodePNG(uzShot));
  }
  if (uz.fault) { console.log(`UZDoom: ${uz.fault} (${uzShot})`); return 2; }
  console.log(`${"UZDoom".padEnd(8)} ${uz.quarters.join(" ")}`);
  let failed = 0;
  for (const api of parityApis(args)) {
    const shot = await oursShot(file, "MAP01", { port, minTic: 35, tag: "flipparity", api });
    const ours = readSprites(decodePNG(shot));
    const faults = ours.fault ? [ours.fault] : judgeFlips(uz.quarters, ours.quarters);
    if (!ours.fault) console.log(`${api.padEnd(8)} ${ours.quarters.join(" ")}`);
    console.log(faults.length ? `FAIL ${api}: ${faults.join("; ")} (${shot})` : `PASS ${api}: ${ACTORS.length} sprites read as UZDoom's`);
    failed += faults.length ? 1 : 0;
  }
  console.log(`shots in ${out}`);
  return failed ? 1 : 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) process.exit(await main(process.argv.slice(2)));
