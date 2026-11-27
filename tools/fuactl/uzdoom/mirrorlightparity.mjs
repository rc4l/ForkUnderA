// [Doruk-Aksoy] Mirror and light render flag parity with UZDoom: three sprites behind the player in front of a line mirror (MAP01) and before it (MAP02),
// plain, INVISIBLEINMIRRORS and ONLYVISIBLEINMIRRORS, read by colour, and two grey sprites beside a red light (MAP03), plain and NODYNAMICLIGHTING, read as lit or not.
// usage: node uzdoom/mirrorlightparity.mjs [--port 7874] [--api vulkan|opengl|both] [--uzdoom-backend gl|vulkan]   (FUACTL_ENGINE picks our binary)
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG, decodePNG, px } from "../src/pngcore.mjs";
import { COLOURS, colourOf, spriteSpans } from "./flipparity.mjs";

const W = 48, H = 64;
const solid = (rgb) => { const b = Buffer.alloc(W * H * 3); for (let i = 0; i < W * H; i++) b.set(rgb, i * 3); return encodePNG(W, H, b, { grab: [W / 2, H] }); };

// [Doruk-Aksoy] The mirror sprites left to right, each its own colour.
export const MIRROR_ACTORS = [
  { cls: "ZxMirrorPlain", sprite: "ZXMR", colour: "R", flags: [] },
  { cls: "ZxMirrorHidden", sprite: "ZXMY", colour: "Y", flags: ["INVISIBLEINMIRRORS"] },
  { cls: "ZxMirrorOnly", sprite: "ZXMB", colour: "B", flags: ["ONLYVISIBLEINMIRRORS"] },
];
// [Doruk-Aksoy] Kept clear of the middle, where the player's own reflection stands.
const MIRROR_XS = [220, 380, 740];
export const LIGHT_ACTORS = [
  { cls: "ZxLightPlain", sprite: "ZXLG", flags: [] },
  { cls: "ZxLightNone", sprite: "ZXLG", flags: ["NODYNAMICLIGHTING"] },
];

const actorDef = (a, num) => [`actor ${a.cls} ${num}`, "{", "\tRadius 16", "\tHeight 64", "\tScale 2", "\t+NOGRAVITY", "\t+NOINTERACTION",
  ...a.flags.map((f) => `\t+${f}`), "\tStates", "\t{", "\tSpawn:", `\t\t${a.sprite} A -1`, "\t\tStop", "\t}", "}"].join("\n");

const ROOM = (tex, light) => ({ heightfloor: 0, heightceiling: 256, texturefloor: tex, textureceiling: tex, lightlevel: light });

// [Doruk-Aksoy] MAP01 has the north wall a mirror and the sprites behind the player; MAP02 has them before the player and no mirror.
function mirrorMap(mirror) {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM("ZXGRAY", 255), (edge) => ({ side: { texturemiddle: "ZXGRAY" }, line: mirror && edge === "north" ? { special: 182 } : {} }));
  MIRROR_ACTORS.forEach((a, i) => m.thing({ x: MIRROR_XS[i], y: mirror ? 120 : 700, angle: 90, type: 20270 + i }));
  m.thing({ x: 512, y: 300, angle: 90, type: 1 });
  return m;
}

// [Doruk-Aksoy] MAP03: a dark black room, the two grey sprites either side of a red light the player looks at.
function lightMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM("ZXBLACK", 160), () => ({ side: { texturemiddle: "ZXBLACK" } }));
  LIGHT_ACTORS.forEach((a, i) => m.thing({ x: 448 + 128 * i, y: 520, angle: 90, type: 20280 + i }));
  m.thing({ x: 512, y: 520, angle: 0, type: 9800, height: 40, arg0: 255, arg1: 0, arg2: 0, arg3: 112 });
  m.thing({ x: 512, y: 200, angle: 90, type: 1 });
  return m;
}

export function buildMirrorLightPk3() {
  const decorate = [...MIRROR_ACTORS.map((a, i) => actorDef(a, 20270 + i)), ...LIGHT_ACTORS.map((a, i) => actorDef(a, 20280 + i))].join("\n") + "\n";
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", mirrorMap(true))) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", mirrorMap(false))) },
    { name: "maps/MAP03.wad", data: wad(mapLumps("MAP03", lightMap())) },
    { name: "decorate.txt", data: decorate },
    ...MIRROR_ACTORS.map((a) => ({ name: `sprites/${a.sprite}A0.png`, data: solid(COLOURS[a.colour]) })),
    { name: "sprites/ZXLGA0.png", data: solid([160, 160, 160]) },
    { name: "textures/ZXGRAY.png", data: encodePNG(64, 64, Buffer.alloc(64 * 64 * 3, 128)) },
    { name: "textures/ZXBLACK.png", data: encodePNG(64, 64, Buffer.alloc(64 * 64 * 3, 0)) },
  ]);
}

// [Doruk-Aksoy] The colours of the sprites a picture shows, left to right, each read at its span's middle; a run under minWidth columns (the player's own small reflection) is left out.
export function readColours(img, minWidth = 16) {
  const mid = Math.floor(img.h * 0.45);
  return spriteSpans(img).filter(([x0, x1]) => x1 - x0 >= minWidth).map(([x0, x1]) => {
    for (let dy = 0; dy < img.h * 0.3; dy++)
      for (const y of [mid - dy, mid + dy]) { const c = colourOf(px(img, Math.floor((x0 + x1) / 2), y)); if (c) return c; }
    return "-";
  }).join("");
}

// [Doruk-Aksoy] Between the top eighth and the status bar, the column runs holding anything brighter than the black room: the grey sprites, left to right, each "lit" when the red light tints it.
export function readLit(img, floor = 24, gap = 8) {
  const top = Math.floor(img.h * 0.125), bottom = Math.floor(img.h * 0.8);
  const bright = (x, y) => Math.max(...px(img, x, y)) > floor;
  const spans = [];
  let start = -1, last = -1;
  for (let x = 0; x < img.w; x++) {
    let hit = false;
    for (let y = top; y < bottom && !hit; y++) hit = bright(x, y);
    if (!hit) continue;
    if (start < 0 || x - last > gap) { if (start >= 0) spans.push([start, last + 1]); start = x; }
    last = x;
  }
  if (start >= 0) spans.push([start, last + 1]);
  return spans.map(([x0, x1]) => {
    let r = 0, b = 0;
    for (let y = top; y < bottom; y++) for (let x = x0; x < x1; x++) if (bright(x, y)) { const p = px(img, x, y); r += p[0]; b += p[2]; }
    return r > b * 1.25 ? "lit" : "unlit";
  });
}

// [Doruk-Aksoy] Each map, what it shows and how a picture of it reads.
export const MAPS = [
  { map: "MAP01", what: "a mirror: plain and ONLYVISIBLEINMIRRORS reflected, INVISIBLEINMIRRORS not", read: (img) => readColours(img) },
  { map: "MAP02", what: "no mirror: plain and INVISIBLEINMIRRORS, ONLYVISIBLEINMIRRORS not", read: (img) => readColours(img) },
  { map: "MAP03", what: "a red light: plain lit, NODYNAMICLIGHTING not", read: (img) => readLit(img).join(" ") },
];

async function main(args) {
  const { uzdoomShot, oursShot } = await import("./shots.mjs");
  const { parityApis, parityUzdoomBackend } = await import("../src/uzdoom.mjs");
  const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : null);
  const port = Number(flag("--port")) || 7874;
  const out = fs.mkdtempSync(path.join(os.tmpdir(), "mirrorlightparity-"));
  const file = path.join(out, "mirrorlightparity.pk3");
  fs.writeFileSync(file, buildMirrorLightPk3());
  let failed = 0;
  for (const m of MAPS) {
    const uz = m.read(decodePNG(uzdoomShot(file, m.map, out, parityUzdoomBackend(args))));
    console.log(`${m.map} ${m.what}\n  ${"UZDoom".padEnd(8)} ${uz}`);
    for (const api of parityApis(args)) {
      const shot = await oursShot(file, m.map, { port, minTic: 35, tag: "mirrorlight", api });
      const ours = m.read(decodePNG(shot));
      const ok = ours === uz && uz.length > 0;
      console.log(`  ${api.padEnd(8)} ${ours}  ${ok ? "ok" : `MISMATCH (${shot})`}`);
      failed += ok ? 0 : 1;
    }
  }
  console.log(`${failed ? "FAIL" : "PASS"}: ${failed} mismatch(es); shots in ${out}`);
  return failed ? 1 : 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) process.exit(await main(process.argv.slice(2)));
