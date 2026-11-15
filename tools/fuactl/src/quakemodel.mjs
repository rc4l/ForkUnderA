// [Doruk-Aksoy] Quake 2 MD2 and Quake 3 MD3 models written from vertices, for template maps that need a model file of either format, animated or not.

// [Doruk-Aksoy] A fixed-width, NUL-padded name field.
function name(buf, at, text, width) {
  buf.fill(0, at, at + width);
  buf.write(text.slice(0, width - 1), at, "latin1");
}

// [Doruk-Aksoy] One MD3 surface over keyframes `frames` (each [[x, y, z], ...], z up, in model units at 1/64 precision), triangles `tris` ([[a, b, c], ...]) and texture coordinates `uvs` ([[s, t], ...]), as FMD3Model::Load reads it.
export function md3({ frames, tris, uvs, label = "zx", shader = "" }) {
  const nverts = uvs.length, nframes = frames.length;
  if (!nframes || frames.some((f) => f.length !== nverts)) throw new Error("md3: every frame needs one position per texture coordinate");
  const surfHeader = 108, shaderSize = 68;
  const ofsShaders = surfHeader, ofsTris = ofsShaders + shaderSize, ofsST = ofsTris + tris.length * 12, ofsXyz = ofsST + nverts * 8;
  const surfEnd = ofsXyz + nverts * nframes * 8;
  const header = 108, frameSize = 56;
  const ofsFrames = header, ofsSurfaces = ofsFrames + nframes * frameSize, ofsEnd = ofsSurfaces + surfEnd;
  const b = Buffer.alloc(ofsEnd);
  b.write("IDP3", 0, "latin1");
  b.writeInt32LE(15, 4);
  name(b, 8, label, 64);
  const ints = [0, nframes, 0, 1, 0, ofsFrames, ofsSurfaces, ofsSurfaces, ofsEnd];
  ints.forEach((v, i) => b.writeInt32LE(v, 72 + i * 4));
  frames.forEach((f, k) => {
    const at = ofsFrames + k * frameSize;
    for (let c = 0; c < 3; c++) {
      b.writeFloatLE(Math.min(...f.map((p) => p[c])), at + c * 4);
      b.writeFloatLE(Math.max(...f.map((p) => p[c])), at + 12 + c * 4);
    }
    b.writeFloatLE(Math.max(...f.map((p) => Math.hypot(...p))), at + 36);
    name(b, at + 40, `frame${k}`, 16);
  });
  const s = ofsSurfaces;
  b.write("IDP3", s, "latin1");
  name(b, s + 4, label, 64);
  [0, nframes, 1, nverts, tris.length, ofsTris, ofsShaders, ofsST, ofsXyz, surfEnd].forEach((v, i) => b.writeInt32LE(v, s + 68 + i * 4));
  name(b, s + ofsShaders, shader, 64);
  tris.forEach((t, i) => t.forEach((v, c) => b.writeInt32LE(v, s + ofsTris + i * 12 + c * 4)));
  uvs.forEach(([u, v], i) => { b.writeFloatLE(u, s + ofsST + i * 8); b.writeFloatLE(v, s + ofsST + i * 8 + 4); });
  frames.forEach((f, k) => f.forEach((p, i) => {
    const at = s + ofsXyz + (k * nverts + i) * 8;
    p.forEach((c, j) => b.writeInt16LE(Math.round(c * 64), at + j * 2));
  }));
  return b;
}

// [Doruk-Aksoy] An MD2 over keyframes `frames` (each [[x, y, z], ...], z up, packed to bytes over each frame's own box), triangles `tris` and texture coordinates `uvs` in pixels of a skinW by skinH skin, as FMD2Model::Load reads it; its one GL command is never drawn.
export function md2({ frames, tris, uvs, skinW = 64, skinH = 64, skin = "" }) {
  const nverts = frames[0]?.length ?? 0, nframes = frames.length;
  if (!nframes || frames.some((f) => f.length !== nverts)) throw new Error("md2: every frame needs the same vertices");
  const header = 68, frameSize = 40 + nverts * 4;
  const ofsSkins = header, ofsST = ofsSkins + (skin ? 64 : 0), ofsTris = ofsST + uvs.length * 4;
  const ofsFrames = ofsTris + tris.length * 12, ofsGl = ofsFrames + nframes * frameSize, ofsEnd = ofsGl + 4;
  const b = Buffer.alloc(ofsEnd);
  b.write("IDP2", 0, "latin1");
  [8, skinW, skinH, frameSize, skin ? 1 : 0, nverts, uvs.length, tris.length, 1, nframes, ofsSkins, ofsST, ofsTris, ofsFrames, ofsGl, ofsEnd]
    .forEach((v, i) => b.writeInt32LE(v, 4 + i * 4));
  if (skin) name(b, ofsSkins, skin, 64);
  uvs.forEach(([u, v], i) => { b.writeInt16LE(u, ofsST + i * 4); b.writeInt16LE(v, ofsST + i * 4 + 2); });
  // [Doruk-Aksoy] A triangle is its vertex indices then its texture coordinate indices, a vertex's own coordinate here.
  tris.forEach((t, i) => t.forEach((v, c) => { b.writeInt16LE(v, ofsTris + i * 12 + c * 2); b.writeInt16LE(v, ofsTris + i * 12 + 6 + c * 2); }));
  frames.forEach((f, k) => {
    const at = ofsFrames + k * frameSize;
    const lo = [0, 1, 2].map((c) => Math.min(...f.map((p) => p[c])));
    const hi = [0, 1, 2].map((c) => Math.max(...f.map((p) => p[c])));
    const scale = lo.map((l, c) => (hi[c] - l) / 255 || 1);
    scale.forEach((v, c) => b.writeFloatLE(v, at + c * 4));
    lo.forEach((v, c) => b.writeFloatLE(v, at + 12 + c * 4));
    name(b, at + 24, `frame${k}`, 16);
    f.forEach((p, i) => p.forEach((c, j) => b.writeUInt8(Math.round((c - lo[j]) / scale[j]), at + 40 + i * 4 + j)));
  });
  return b;
}
