// [rc4l] A small UDMF map writer, so a regression carries its own level and needs only an IWAD -- the engine builds the nodes.
import { Buffer } from "node:buffer";

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

  // [rc4l] A closed rectangular room walked clockwise, so it is on every wall's right; walls(edge) returns { side, line } per edge.
  room(x0, y0, x1, y1, sectorProps, walls = () => ({})) {
    const s = this.sector(sectorProps);
    for (const [name, ax, ay, bx, by] of rectEdges(x0, y0, x1, y1)) {
      const w = walls(name) || {};
      this.wall(ax, ay, bx, by, s, w.side || {}, w.line || {});
    }
    return s;
  }

  // [rc4l] A sector standing inside another, joined by two-sided lines so the block is on every line's front; outer(edge) styles the side facing out.
  block(x0, y0, x1, y1, sectorProps, outerSector, outer = () => ({})) {
    const s = this.sector(sectorProps);
    for (const [name, ax, ay, bx, by] of rectEdges(x0, y0, x1, y1)) {
      this.sides.push({ sector: s });
      this.sides.push({ sector: outerSector, ...(outer(name) || {}) });
      this.lines.push({ v1: this.vertex(ax, ay), v2: this.vertex(bx, by), sidefront: this.sides.length - 2,
        sideback: this.sides.length - 1, twosided: true });
    }
    return s;
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
