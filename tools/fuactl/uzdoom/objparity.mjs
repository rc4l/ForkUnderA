// [Doruk-Aksoy] OBJ parity with UZDoom, by eye: an OBJ "axes" model (an arm along +X, +Y and +Z, each its own texture) and the octagonal prism,
// seen from a player start south of them (MAP01) and east of them (MAP02); writes the pk3 and, with --shots, each engine's picture of each map.
// usage: node uzdoom/objparity.mjs <out.pk3> [--shots [--port 7874] [--api vulkan|opengl|both] [--uzdoom-backend gl|vulkan]]   (CULL=1 adds FORCECULLBACKFACES, which shows whether the engines wind triangles alike)
import fs, { writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { prismObj } from "../scenarios/objmodel.mjs";
import { uzdoomShot, oursShot } from "./shots.mjs";
import { parityApis, parityUzdoomBackend } from "../src/uzdoom.mjs";

// [Doruk-Aksoy] A box from (x0, y0, z0) to (x1, y1, z1) in OBJ space, its six faces as quads after `base` vertices already written.
function box(lines, base, [x0, y0, z0], [x1, y1, z1]) {
  const c = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
  for (const [x, y, z] of c) lines.push(`v ${x} ${y} ${z}`);
  const quads = [[1, 2, 3, 4], [5, 8, 7, 6], [1, 5, 6, 2], [2, 6, 7, 3], [3, 7, 8, 4], [4, 8, 5, 1]];
  for (const q of quads) lines.push(`f ${q.map((i) => `${base + i}/${((i - 1) % 4) + 1}`).join(" ")}`);
}

// [Doruk-Aksoy] Arms 64 long and 12 thick from the origin: +X in STARTAN2, +Y (up) in COMPBLUE, +Z in BROWN1, and a hub in FLAT23.
function axesObj() {
  const lines = ["# axes", "vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1"];
  let base = 0;
  const arm = (material, from, to) => { lines.push(`usemtl ${material}`); box(lines, base, from, to); base += 8; };
  arm("FLAT23", [-8, 0, -8], [8, 16, 8]);
  arm("STARTAN2", [8, 2, -6], [72, 14, 6]);
  arm("COMPBLUE", [-6, 16, -6], [6, 80, 6]);
  arm("BROWN1", [-6, 2, 8], [6, 14, 72]);
  return lines.join("\n") + "\n";
}

const decorate = ["ZXParityAxes 20230", "ZXParityPrism 20231"].map((s) => {
  const [name, num] = s.split(" ");
  return `Actor ${name} ${num}\n{\n  Radius 16\n  Height 64\n  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`;
}).join("\n");
const modeldef = [["ZXParityAxes", "axes.obj"], ["ZXParityPrism", "prism.obj"]].map(([name, file]) =>
  `Model ${name}\n{\n  Path "models/"\n  Model 0 "${file}"\n  Scale 1.0 1.0 1.0\n${process.env.CULL ? "  FORCECULLBACKFACES\n" : ""}  FrameIndex ELEC A 0 0\n}\n`).join("\n");

// [Doruk-Aksoy] The axes at (512, 512) facing north (angle 90), the prism beside it at (640, 512); the player south of them or east of them.
function room(start) {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 224 },
    () => ({ side: { texturemiddle: "BROWN96" } }));
  m.thing({ x: 512, y: 512, angle: 90, type: 20230 });
  m.thing({ x: 640, y: 512, angle: 90, type: 20231 });
  m.thing({ ...start, type: 1 });
  return m;
}

writeFileSync(process.argv[2], pk3([
  { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room({ x: 560, y: 300, angle: 90 }))) },
  { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room({ x: 800, y: 760, angle: 221 }))) },
  { name: "decorate.txt", data: decorate },
  { name: "modeldef.txt", data: modeldef },
  { name: "models/axes.obj", data: axesObj() },
  { name: "models/prism.obj", data: prismObj() },
]));
console.log("wrote", process.argv[2]);

if (process.argv.includes("--shots")) {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), "objparity-"));
  const port = Number(process.argv[process.argv.indexOf("--port") + 1]) || 7874;
  const args = process.argv.slice(2), uzBackend = parityUzdoomBackend(args);
  for (const map of ["MAP01", "MAP02"]) {
    console.log(`${map} UZDoom ${uzBackend}: ${uzdoomShot(process.argv[2], map, out, uzBackend)}`);
    for (const api of parityApis(args)) console.log(`${map} ours ${api}: ${await oursShot(process.argv[2], map, { port, tag: "objparity", api })}`);
  }
}
