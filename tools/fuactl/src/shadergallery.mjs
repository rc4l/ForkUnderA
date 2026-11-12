// [Doruk-Aksoy] A shader gallery: a mod's GLDEFS shader, material and brightmap blocks for chosen textures, with every lump they name, each texture on a lit wall of its own map, so two engines can be shot on the same pk3.
import fs from "node:fs";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3, readPk3 } from "./udmf.mjs";

// [Doruk-Aksoy] A pk3 or zip's files, or a wad's lumps under their own names.
export function readArchive(file) {
  const buf = fs.readFileSync(file);
  if (buf.toString("latin1", 0, 4) !== "PWAD" && buf.toString("latin1", 0, 4) !== "IWAD") return readPk3(buf);
  const count = buf.readUInt32LE(4), dir = buf.readUInt32LE(8);
  return Array.from({ length: count }, (_, i) => {
    const at = dir + 16 * i;
    return { name: buf.toString("latin1", at + 8, at + 16).replace(/\0.*$/, ""), data: buf.subarray(buf.readUInt32LE(at), buf.readUInt32LE(at) + buf.readUInt32LE(at + 4)) };
  });
}

const lower = (s) => s.toLowerCase();
const base = (name) => lower(path.posix.basename(name).replace(/\.[^.]*$/, ""));

// [Doruk-Aksoy] Where a lump names a texture, a map or a shader by its full path or by its short name in a texture folder, as the engine looks it up.
const TEXTURE_DIRS = ["textures", "flats", "graphics", "patches", "sprites", "hires", "brightmaps", "materials"];
export function resolveLump(files, ref) {
  const want = lower(ref);
  const exact = files.find((f) => lower(f.name) === want);
  if (exact) return exact;
  return files.find((f) => base(f.name) === base(ref) && TEXTURE_DIRS.includes(lower(f.name).split("/")[0])) || null;
}

// [Doruk-Aksoy] Every GLDEFS-family lump's text with its #includes in place, the game's own first, as gl_ParseDefs reads them.
export function gldefsText(files) {
  const roots = files.filter((f) => /^(gldefs|doomdefs)(\.|$)/i.test(path.posix.basename(f.name)) && !f.name.includes("/"));
  const seen = new Set();
  const expand = (f, depth) => {
    if (depth > 16 || seen.has(f.name)) return "";
    seen.add(f.name);
    return f.data.toString("latin1").replace(/#include\s+(?:"([^"]+)"|(\S+))/gi, (_, quoted, bare) => {
      const g = files.find((x) => lower(x.name) === lower(quoted ?? bare));
      return g ? expand(g, depth + 1) : "";
    });
  };
  return roots.map((f) => expand(f, 0)).join("\n");
}

// [Doruk-Aksoy] A GLDEFS text's words and quoted strings, comments left out, each with where it ends.
function tokens(text) {
  const out = [];
  const re = /\/\/[^\n]*|\/\*[\s\S]*?\*\/|"((?:[^"\\]|\\.)*)"|([{}])|([^\s{}"]+)/g;
  for (let m; (m = re.exec(text));) {
    if (m[1] !== undefined) out.push({ text: m[1], quoted: true, start: m.index, end: re.lastIndex });
    else if (m[2] || m[3]) out.push({ text: m[2] || m[3], quoted: false, start: m.index, end: re.lastIndex });
  }
  return out;
}

const KINDS = ["hardwareshader", "material", "brightmap"];
const TYPES = ["texture", "flat", "sprite"];

// [Doruk-Aksoy] The HardwareShader, Material and brightmap blocks whose target is one of `names` (any case), as written, and each one's target and kind.
export function galleryBlocks(text, names) {
  const want = new Set(names.map(lower));
  const t = tokens(text);
  const out = [];
  for (let k = 0; k < t.length; k++) {
    if (t[k].quoted || !KINDS.includes(lower(t[k].text))) continue;
    let n = k + 1;
    if (n < t.length && !t[n].quoted && TYPES.includes(lower(t[n].text))) n++;
    if (n >= t.length || lower(t[n].text) === "postprocess" || t[n + 1]?.text !== "{") continue;
    let close = n + 1;
    for (let depth = 0; close < t.length; close++) {
      if (t[close].quoted) continue;
      if (t[close].text === "{") depth++;
      else if (t[close].text === "}" && --depth === 0) break;
    }
    if (want.has(lower(t[n].text))) out.push({ kind: lower(t[k].text), target: t[n].text, text: text.slice(t[k].start, t[Math.min(close, t.length - 1)].end) });
    k = close;
  }
  return out;
}

// [Doruk-Aksoy] Keywords whose next word names a lump: a shader, a map, or (after its own name) a Texture line's picture.
const LUMP_WORDS = ["shader", "map", "brightmap", "normal", "specular", "metallic", "roughness", "ao"];

// [Doruk-Aksoy] The lumps a block names inside its braces, besides its target.
export function referencedNames(block) {
  const t = tokens(block);
  const out = [];
  for (let k = t.findIndex((x) => !x.quoted && x.text === "{") + 1; k > 0 && k < t.length; k++) {
    if (t[k].quoted) continue;
    const w = lower(t[k].text);
    if (LUMP_WORDS.includes(w) && k + 1 < t.length) out.push(t[++k].text);
    else if (w === "texture" && k + 2 < t.length) { k += 2; out.push(t[k].text); }
  }
  return out;
}

// [Doruk-Aksoy] The TEXTURES lumps' definitions of `names` (any case), as written, the patches they draw from, and the names they define.
export function textureDefinitions(files, names) {
  const want = new Set(names.map(lower));
  const defs = [], patches = [], defined = new Set();
  for (const f of files.filter((x) => !x.name.includes("/") && /^textures(\.|$)/i.test(x.name))) {
    const text = f.data.toString("latin1");
    const re = /\b(?:Texture|WallTexture|Flat|Sprite|Graphic)\s+"?([^",\s]+)"?\s*,[^{]*\{([^}]*)\}/gi;
    for (let m; (m = re.exec(text));) {
      if (!want.has(lower(m[1]))) continue;
      defs.push(m[0]);
      defined.add(lower(m[1]));
      for (const p of m[2].matchAll(/\bPatch\s+"?([^",]+?)"?\s*,/gi)) patches.push(p[1]);
    }
  }
  return { text: defs.join("\n\n"), patches, defined };
}

const mapName = (n) => `MAP${String(n).padStart(2, "0")}`;

// [Doruk-Aksoy] A room lit at `light`, the texture on its north wall, a white point light before it unless `dark`, and the player facing it.
function galleryRoom(texture, { light = 128, dark = false } = {}) {
  const m = new UdmfMap();
  m.room(0, 0, 384, 384, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: light },
    (edge) => ({ side: { texturemiddle: edge === "north" ? texture : "STARTAN2" } }));
  if (!dark) m.thing({ x: 160, y: 320, height: 56, type: 9800, arg0: 255, arg1: 255, arg2: 255, arg3: 200 });
  m.thing({ x: 192, y: 140, angle: 90, type: 1 });
  return m;
}

// [Doruk-Aksoy] The gallery for `textures` of these files: their blocks (each shader or Material block given `extra` lines), the lumps they name, a map per texture, and what was not found.
export function buildGallery(files, textures, { extra = "", light = 128, dark = false } = {}) {
  const blocks = galleryBlocks(gldefsText(files), textures);
  const missing = [];
  const lumps = new Map();
  const take = (ref) => {
    const f = resolveLump(files, ref);
    if (f) lumps.set(lower(f.name), f);
    else missing.push(ref);
  };
  for (const b of blocks) referencedNames(b.text).forEach(take);
  // [Doruk-Aksoy] A texture TEXTURES defines comes with its definition and patches, else as a picture of its own name.
  const defined = textureDefinitions(files, textures);
  defined.patches.forEach(take);
  textures.filter((t) => !defined.defined.has(lower(t))).forEach(take);
  const gldefs = blocks.map((b) => (b.kind !== "brightmap" && extra ? b.text.replace(/\}\s*$/, `\t${extra}\n}`) : b.text)).join("\n") + "\n";
  const maps = textures.map((t, i) => ({ map: mapName(i + 1), texture: t }));
  const data = pk3([
    ...[...lumps.values()].map((f) => ({ name: f.name, data: f.data })),
    { name: "gldefs.txt", data: gldefs },
    ...(defined.text ? [{ name: "textures.txt", data: defined.text + "\n" }] : []),
    ...maps.map(({ map, texture }) => ({ name: `maps/${map}.wad`, data: wad(mapLumps(map, galleryRoom(texture, { light, dark }))) })),
  ]);
  return { pk3: data, maps, blocks, missing };
}
