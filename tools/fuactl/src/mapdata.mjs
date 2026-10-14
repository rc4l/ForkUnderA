// [rc4l] A level's own data read from its wad or pk3, so "what is this sector" is answered from the files instead of a one-off script.
import zlib from "node:zlib";
import { readWad, parseUdmf } from "./udmf.mjs";

// [rc4l] The lumps that belong to a map after its marker, in either format.
const MAP_LUMPS = new Set(["TEXTMAP", "ZNODES", "BEHAVIOR", "SCRIPTS", "DIALOGUE", "ENDMAP", "THINGS", "LINEDEFS",
  "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"]);

// [rc4l] A zip's (pk3's) entries by name, each inflated only when read, since a mod pk3 can hold hundreds of megabytes.
export function readZip(buf) {
  let eocd = -1;
  for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) {
    if (buf.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error("not a zip: no end-of-central-directory record");
  const entries = new Map();
  let at = buf.readUInt32LE(eocd + 16);
  for (let i = 0; i < buf.readUInt16LE(eocd + 10); i++) {
    if (buf.readUInt32LE(at) !== 0x02014b50) throw new Error("zip central directory is corrupt");
    const method = buf.readUInt16LE(at + 10), size = buf.readUInt32LE(at + 20);
    const nameLen = buf.readUInt16LE(at + 28), extraLen = buf.readUInt16LE(at + 30), commentLen = buf.readUInt16LE(at + 32);
    const local = buf.readUInt32LE(at + 42);
    const name = buf.toString("latin1", at + 46, at + 46 + nameLen);
    const start = local + 30 + buf.readUInt16LE(local + 26) + buf.readUInt16LE(local + 28);
    const raw = buf.subarray(start, start + size);
    entries.set(name, () => (method === 8 ? zlib.inflateRawSync(raw) : Buffer.from(raw)));
    at += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}

// [rc4l] A wad's lumps for one map, from its marker to the next lump that is not a map lump; null when the wad has no such map.
function mapFromWad(lumps, mapName) {
  const at = lumps.findIndex((l) => l.name.toUpperCase() === mapName.toUpperCase());
  if (at < 0) return null;
  const out = new Map();
  for (let i = at + 1; i < lumps.length && MAP_LUMPS.has(lumps[i].name.toUpperCase()); i++) out.set(lumps[i].name.toUpperCase(), lumps[i].data);
  return out;
}

// [rc4l] A map's lumps from the files a game loads, the LAST file that has it winning as the engine's load order does.
export function findMapLumps(files, mapName) {
  let found = null;
  for (const { name, buf } of files) {
    let lumps = null;
    if (buf.toString("latin1", 0, 2) === "PK") {
      const want = `maps/${mapName}.wad`.toLowerCase();
      const entry = [...readZip(buf)].find(([n]) => n.toLowerCase() === want);
      // [rc4l] A pk3 map wad's marker is whatever its author named it, so its lumps are taken from the first one.
      if (entry) { const inner = readWad(entry[1]()); lumps = mapFromWad(inner, inner[0].name); }
    } else {
      lumps = mapFromWad(readWad(buf), mapName);
    }
    if (lumps) found = { file: name, lumps };
  }
  return found;
}

// [rc4l] What a question about a map needs first: its size, which line specials it uses and how often, and how much of it is open sky.
export function summarizeMap(udmf) {
  const specials = {};
  for (const l of udmf.linedef) if (l.special) specials[l.special] = (specials[l.special] || 0) + 1;
  const sky = (plane) => udmf.sector.filter((s) => String(s[plane] || "").toUpperCase() === "F_SKY1").length;
  return { namespace: udmf.namespace, sectors: udmf.sector.length, lines: udmf.linedef.length, things: udmf.thing.length,
    specials, skyCeilings: sky("textureceiling"), skyFloors: sky("texturefloor") };
}

// [rc4l] One sector's own fields and the lines that bound it, front or back.
export function describeSector(udmf, index) {
  const sector = udmf.sector[index];
  if (!sector) throw new Error(`no sector ${index}: the map has ${udmf.sector.length}`);
  const sideIn = (s) => s !== undefined && udmf.sidedef[s] && udmf.sidedef[s].sector === index;
  const lines = udmf.linedef.map((l, i) => (sideIn(l.sidefront) || sideIn(l.sideback) ? i : -1)).filter((i) => i >= 0);
  const xs = [], ys = [];
  for (const i of lines) for (const v of [udmf.linedef[i].v1, udmf.linedef[i].v2]) { xs.push(udmf.vertex[v].x); ys.push(udmf.vertex[v].y); }
  // [rc4l] The box its lines span, so a camera or a player can be put inside it without guessing.
  const bounds = lines.length ? [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)] : null;
  return { index, ...sector, bounds, lines };
}

// [rc4l] Every sector carrying a tag (its UDMF id), described: what a special or script aimed at that tag reaches.
export function describeTag(udmf, tag) {
  return udmf.sector.map((s, i) => (s.id === tag ? describeSector(udmf, i) : null)).filter(Boolean);
}

// [rc4l] One line with its ends, its special and the sector on each side.
export function describeLine(udmf, index) {
  const line = udmf.linedef[index];
  if (!line) throw new Error(`no line ${index}: the map has ${udmf.linedef.length}`);
  const sectorOf = (s) => (s === undefined ? null : udmf.sidedef[s].sector);
  const v = (i) => [udmf.vertex[i].x, udmf.vertex[i].y];
  // [rc4l] Each side's own fields too, since its light, offsets and flags decide how it is drawn and the sector does not say.
  const side = (s) => (s === undefined ? null : udmf.sidedef[s]);
  return { index, ...line, from: v(line.v1), to: v(line.v2), front: sectorOf(line.sidefront), back: sectorOf(line.sideback),
    frontSide: side(line.sidefront), backSide: side(line.sideback) };
}

// [rc4l] The numbered lines of a map's ACS source that match, since an OPEN script can change what the map's own data says.
export function grepScripts(text, pattern) {
  const re = new RegExp(pattern, "i");
  return String(text).split(/\r?\n/).map((t, i) => ({ line: i + 1, text: t.trim() })).filter((l) => re.test(l.text));
}

// [rc4l] Everything `fuactl mapdata` prints, from the map's lumps and the sectors, lines, special and script pattern asked about.
export function mapReport(found, { sectors = [], lines = [], scripts = null, special = null, tag = null, things = [] } = {}) {
  const textmap = found.lumps.get("TEXTMAP");
  if (!textmap) throw new Error(`${found.file} holds the map in binary format; mapdata reads UDMF only`);
  const udmf = parseUdmf(textmap.toString("latin1"));
  const source = found.lumps.get("SCRIPTS");
  return {
    file: found.file,
    summary: summarizeMap(udmf),
    sectors: sectors.map((i) => describeSector(udmf, i)),
    lines: lines.map((i) => describeLine(udmf, i)),
    tag: tag === null ? undefined : describeTag(udmf, tag),
    things: things.length === 0 ? undefined
      : udmf.thing.map((t, index) => (things.includes(t.type) ? { index, ...t } : null)).filter(Boolean),
    special: special === null ? undefined
      : udmf.linedef.map((l, i) => (l.special === special ? describeLine(udmf, i) : null)).filter(Boolean),
    scripts: scripts === null ? undefined : (source ? grepScripts(source.toString("latin1"), scripts) : "no SCRIPTS source in the map"),
  };
}
