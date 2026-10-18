// [rc4l] A small UDMF map writer, so a regression carries its own level and needs only an IWAD -- the engine builds the nodes.
import { THING } from "./doomnums.mjs";
import { Buffer } from "node:buffer";
import zlib from "node:zlib";

// [rc4l] A float written bare with its point, since UDMF refuses a quoted number.
class Float {
  constructor(v) { this.text = Number.isInteger(v) ? `${v}.0` : String(v); }
}
const num = (v) => new Float(v);

function field(key, value) {
  if (value instanceof Float) return `${key} = ${value.text};`;
  if (typeof value === "string") return `${key} = "${value}";`;
  return `${key} = ${value};`;
}

// [rc4l] The skybox room's walls, one texture each, so a sky seen through it shows which way it faces.
const SKYBOX_WALLS = { north: "COMPBLUE", east: "BRICK1", south: "STARTAN2", west: "BROWN1" };

// [rc4l] A rectangle's edges walked clockwise, so whatever is inside lies on each edge's right.
function rectEdges(x0, y0, x1, y1) {
  return [["west", x0, y0, x0, y1], ["north", x0, y1, x1, y1], ["east", x1, y1, x1, y0], ["south", x1, y0, x0, y0]];
}

export class UdmfMap {
  constructor() {
    this.vertices = [];
    this.lines = [];
    this.sides = [];
    this.sectors = [];
    this.things = [];
    this._vertexAt = new Map();
  }

  // [rc4l] One vertex per position, so two rooms sharing a corner share the vertex.
  vertex(x, y) {
    const key = `${x},${y}`;
    if (!this._vertexAt.has(key)) {
      this._vertexAt.set(key, this.vertices.length);
      this.vertices.push({ x, y });
    }
    return this._vertexAt.get(key);
  }

  sector(props) {
    this.sectors.push({ ...props });
    return this.sectors.length - 1;
  }

  // [rc4l] A one-sided wall from (x1, y1) to (x2, y2), facing the sector on its right.
  wall(x1, y1, x2, y2, sector, side = {}, line = {}) {
    this.sides.push({ sector, ...side });
    this.lines.push({ v1: this.vertex(x1, y1), v2: this.vertex(x2, y2), sidefront: this.sides.length - 1,
      blocking: true, ...line });
    return this.lines.length - 1;
  }

  // [rc4l] A two-sided line from (x1, y1) to (x2, y2), front on its right; frontSide and backSide style each side, line the line itself.
  opening(x1, y1, x2, y2, front, back, frontSide = {}, backSide = {}, line = {}) {
    this.sides.push({ sector: front, ...frontSide }, { sector: back, ...backSide });
    this.lines.push({ v1: this.vertex(x1, y1), v2: this.vertex(x2, y2), sidefront: this.sides.length - 2,
      sideback: this.sides.length - 1, twosided: true, ...line });
    return this.lines.length - 1;
  }

  // [rc4l] A closed rectangular room walked clockwise, so it is on every wall's right; walls(edge) returns { side, line } per edge.
  room(x0, y0, x1, y1, sectorProps, walls = () => ({})) {
    const s = this.sector(sectorProps);
    for (const [name, ax, ay, bx, by] of rectEdges(x0, y0, x1, y1)) {
      const w = walls(name) || {};
      this.wall(ax, ay, bx, by, s, w.side || {}, w.line || {});
    }
    return s;
  }

  // [rc4l] A sector standing inside another, joined by two-sided lines so the block is on every line's front; outer(edge) and inner(edge) style the sides facing out and in, line(edge) the lines themselves.
  block(x0, y0, x1, y1, sectorProps, outerSector, outer = () => ({}), inner = () => ({}), line = () => ({})) {
    const s = this.sector(sectorProps);
    for (const [name, ax, ay, bx, by] of rectEdges(x0, y0, x1, y1))
      this.opening(ax, ay, bx, by, s, outerSector, inner(name) || {}, outer(name) || {}, line(name) || {});
    return s;
  }

  // [rc4l] A sector of any convex outline standing inside another, its points clockwise so the shape is on every line's front.
  shape(points, sectorProps, outerSector) {
    const s = this.sector(sectorProps);
    points.forEach(([ax, ay], i) => { const [bx, by] = points[(i + 1) % points.length]; this.opening(ax, ay, bx, by, s, outerSector); });
    return s;
  }

  // [rc4l] A square of self-referencing lines, both sides in one new sector, the trick that hides a sector inside another; GL draws what it encloses as the surrounding sector.
  selfRef(x0, y0, x1, y1, sectorProps) {
    const s = this.sector(sectorProps);
    for (const [, ax, ay, bx, by] of rectEdges(x0, y0, x1, y1)) this.opening(ax, ay, bx, by, s, s);
    return s;
  }

  // [rc4l] A 512 skybox room at (x0, y0) holding the level's default SkyViewpoint, so every sky ceiling in the map shows it.
  skyboxRoom(x0, y0) {
    this.room(x0, y0, x0 + 512, y0 + 512, { heightfloor: 0, heightceiling: 384, texturefloor: "FLOOR0_1",
      textureceiling: "CEIL5_1", lightlevel: 255 }, (edge) => ({ side: { texturemiddle: SKYBOX_WALLS[edge] } }));
    this.thing({ x: x0 + 256, y: y0 + 256, height: 128, angle: 0, type: THING.SkyViewpoint });
  }

  // [rc4l] A 512 room whose ceiling at 128 is a stacked sector opening onto a sky-lit room upperWidth wide, dx east, as WDI's towns see their rooftops; returns both sectors.
  stackedRooms({ dx = 4096, upperWidth = 512, tid = 7 } = {}) {
    const lower = this.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8",
      textureceiling: "CEIL3_5", lightlevel: 128 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
    const upper = this.room(dx, 0, dx + upperWidth, 512, { heightfloor: 128, heightceiling: 640, texturefloor: "FLOOR4_8",
      textureceiling: "F_SKY1", lightlevel: 255 }, () => ({ side: { texturemiddle: "BRICK1" } }));
    // [rc4l] The anchors sit off the eye's line, so a sky rendered from an anchor, the old fallback, sees the room from somewhere else.
    this.thing({ x: 64, y: 64, angle: 0, type: THING.LowerStackLookOnly, id: tid });
    this.thing({ x: dx + 64, y: 64, angle: 0, type: THING.UpperStackLookOnly, id: tid });
    return { lower, upper };
  }

  // [rc4l] Present on every skill and in every mode unless the caller says otherwise.
  thing(props) {
    this.things.push({ skill1: true, skill2: true, skill3: true, skill4: true, skill5: true,
      single: true, coop: true, dm: true, ...props });
  }

  textmap() {
    const out = ['namespace = "zdoom";', ""];
    const block = (kind, obj) => {
      out.push(`${kind}`, "{");
      for (const [k, v] of Object.entries(obj)) out.push(`  ${field(k, v)}`);
      out.push("}", "");
    };
    for (const v of this.vertices) block("vertex", { x: num(v.x), y: num(v.y) });
    for (const l of this.lines) block("linedef", l);
    for (const s of this.sides) block("sidedef", s);
    for (const s of this.sectors) block("sector", s);
    for (const t of this.things) block("thing", { ...t, x: num(t.x), y: num(t.y) });
    return out.join("\n");
  }
}

// [rc4l] A PWAD from named lumps: header, the lumps back to back, then the directory.
export function wad(lumps) {
  const datas = lumps.map((l) => (Buffer.isBuffer(l.data) ? l.data : Buffer.from(l.data || "", "latin1")));
  const dataSize = datas.reduce((n, d) => n + d.length, 0);
  const dirOffset = 12 + dataSize;
  const out = Buffer.alloc(dirOffset + 16 * lumps.length);
  out.write("PWAD", 0, "latin1");
  out.writeInt32LE(lumps.length, 4);
  out.writeInt32LE(dirOffset, 8);
  let at = 12;
  lumps.forEach((l, i) => {
    datas[i].copy(out, at);
    const e = dirOffset + 16 * i;
    out.writeInt32LE(datas[i].length ? at : 0, e);
    out.writeInt32LE(datas[i].length, e + 4);
    out.write(l.name.toUpperCase().padEnd(8, "\0").slice(0, 8), e + 8, "latin1");
    at += datas[i].length;
  });
  return out;
}

// [rc4l] The lumps one UDMF map needs: its marker, its TEXTMAP, a BEHAVIOR when it runs scripts, and ENDMAP.
export function mapLumps(name, map, { behavior = null } = {}) {
  return [{ name, data: "" }, { name: "TEXTMAP", data: map.textmap() },
    ...(behavior ? [{ name: "BEHAVIOR", data: behavior }] : []), { name: "ENDMAP", data: "" }];
}

// [rc4l] A WAD's lumps in directory order, as { name, data } with data a Buffer; wad() written backwards.
export function readWad(buf) {
  const magic = buf.toString("latin1", 0, 4);
  if (magic !== "PWAD" && magic !== "IWAD") throw new Error(`not a wad: starts ${JSON.stringify(magic)}`);
  const n = buf.readInt32LE(4), dir = buf.readInt32LE(8);
  const out = [];
  for (let i = 0; i < n; i++) {
    const e = dir + 16 * i;
    const pos = buf.readInt32LE(e), size = buf.readInt32LE(e + 4);
    out.push({ name: buf.toString("latin1", e + 8, e + 16).replace(/\0.*$/, ""), data: buf.subarray(pos, pos + size) });
  }
  return out;
}

// [rc4l] A pk3 from files by path ({ name, data }), stored rather than deflated: what MODELDEF needs, since it finds a model by full path and a wad's lumps have none.
export function pk3(files) {
  const datas = files.map((f) => (Buffer.isBuffer(f.data) ? f.data : Buffer.from(f.data || "", "latin1")));
  const names = files.map((f) => Buffer.from(f.name, "latin1"));
  const locals = [], central = [];
  let at = 0;
  files.forEach((f, i) => {
    const crc = zlib.crc32(datas[i]);
    // [rc4l] The fields a local header and its central directory entry share, from the version needed on: 1980-01-01, no flags, method 0.
    const common = Buffer.alloc(26);
    common.writeUInt16LE(20, 0);
    common.writeUInt16LE(0x21, 8);
    common.writeUInt32LE(crc, 10);
    common.writeUInt32LE(datas[i].length, 14);
    common.writeUInt32LE(datas[i].length, 18);
    common.writeUInt16LE(names[i].length, 22);
    const local = Buffer.alloc(4);
    local.writeUInt32LE(0x04034b50, 0);
    locals.push(local, common, names[i], datas[i]);
    const entry = Buffer.alloc(46);
    entry.writeUInt32LE(0x02014b50, 0);
    entry.writeUInt16LE(20, 4);
    common.copy(entry, 6);
    entry.writeUInt32LE(at, 42);
    central.push(entry, names[i]);
    at += 30 + names[i].length + datas[i].length;
  });
  const dir = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(files.length, 8);
  end.writeUInt16LE(files.length, 10);
  end.writeUInt32LE(dir.length, 12);
  end.writeUInt32LE(at, 16);
  return Buffer.concat([...locals, dir, end]);
}

// [rc4l] A stored pk3's files in directory order, as { name, data }, each checked against its CRC; pk3() written backwards.
export function readPk3(buf) {
  const endAt = buf.length - 22;
  if (endAt < 0 || buf.readUInt32LE(endAt) !== 0x06054b50) throw new Error("not a pk3: no end of central directory");
  const count = buf.readUInt16LE(endAt + 10);
  let e = buf.readUInt32LE(endAt + 16);
  const out = [];
  for (let i = 0; i < count; i++) {
    const nameLen = buf.readUInt16LE(e + 28), crc = buf.readUInt32LE(e + 16), size = buf.readUInt32LE(e + 20);
    const name = buf.toString("latin1", e + 46, e + 46 + nameLen);
    const local = buf.readUInt32LE(e + 42);
    const start = local + 30 + buf.readUInt16LE(local + 26) + buf.readUInt16LE(local + 28);
    const data = buf.subarray(start, start + size);
    if (zlib.crc32(data) !== crc) throw new Error(`${name}: CRC mismatch`);
    out.push({ name, data });
    e += 46 + nameLen;
  }
  return out;
}

// [rc4l] A UDMF value as written: a quoted string, a boolean, or a number.
function udmfValue(raw) {
  if (raw.startsWith('"')) return raw.slice(1, -1).replace(/\\(.)/g, "$1");
  if (raw === "true" || raw === "false") return raw === "true";
  return Number(raw);
}

// [rc4l] A TEXTMAP read back into its blocks by kind (vertex, linedef, sidedef, sector, thing), keys lower-cased as the engine reads them.
export function parseUdmf(text) {
  const src = String(text).replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\//g, "");
  const ns = src.match(/namespace\s*=\s*"([^"]*)"\s*;/);
  const out = { namespace: ns ? ns[1] : null, vertex: [], linedef: [], sidedef: [], sector: [], thing: [] };
  for (const m of src.matchAll(/(\w+)\s*\{([^}]*)\}/g)) {
    const block = {};
    for (const kv of m[2].matchAll(/(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|[^;]+);/g)) block[kv[1].toLowerCase()] = udmfValue(kv[2].trim());
    (out[m[1].toLowerCase()] ||= []).push(block);
  }
  return out;
}
