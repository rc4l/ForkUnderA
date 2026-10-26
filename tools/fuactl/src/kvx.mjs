// [Doruk-Aksoy] A slab's top-face bit in KVX's visible-face mask, which FVoxelModel::MakeSlabPolys meshes from.
export const KVX_TOP = 16;

// [rc4l] A solid box as a KVX voxel, the model format small enough to write from a template, laid out (header, offset tables, a slab per column, a 768-byte palette of 6-bit values) as R_LoadKVX checks it.
export function kvxBox(sx, sy, sz, { color = [63, 0, 0], faces = 63 } = {}) {
  const offsetSize = (sx + 1) * 4 + sx * (sy + 1) * 2;
  const slab = 3 + sz;                               // [rc4l] ztop, zleng, visible faces, then a colour per voxel
  const voxData = sx * sy * slab;
  const numBytes = 24 + offsetSize + voxData;
  const out = Buffer.alloc(4 + numBytes + 768);
  let at = 0;
  const int = (v) => { out.writeInt32LE(v, at); at += 4; };
  int(numBytes);
  int(sx); int(sy); int(sz);
  // [rc4l] Pivots are 8.8 fixed point: the middle of the base, so the box stands on the floor.
  int(sx * 128); int(sy * 128); int(sz * 256);
  for (let x = 0; x <= sx; x++) int(offsetSize + x * sy * slab);
  for (let x = 0; x < sx; x++)
    for (let y = 0; y <= sy; y++) { out.writeInt16LE(y * slab, at); at += 2; }
  for (let i = 0; i < sx * sy; i++) {
    out[at++] = 0; out[at++] = sz; out[at++] = faces;   // [rc4l] the whole column, every face visible unless `faces` says otherwise
    for (let z = 0; z < sz; z++) out[at++] = 1;
  }
  // [rc4l] Index 1 is the box's colour; the rest of the palette stays black.
  out[at + 3] = color[0]; out[at + 4] = color[1]; out[at + 5] = color[2];
  return out;
}

// [rc4l] The lumps that put a voxel on every frame of a sprite: the voxel namespace markers around a lump named after the sprite.
export function voxelLumps(sprite, data) {
  return [{ name: "VX_START", data: "" }, { name: sprite, data }, { name: "VX_END", data: "" }];
}
