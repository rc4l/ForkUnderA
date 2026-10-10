import { test } from "node:test";
import assert from "node:assert/strict";
import { kvxBox, voxelLumps } from "../src/kvx.mjs";

// [rc4l] R_LoadKVX's own checks, restated: the first x offset is the offset table's size, the last ends the slab data, and the palette fills the tail.
test("a box voxel has the layout R_LoadKVX accepts", () => {
  const v = kvxBox(2, 3, 4);
  const offsetSize = 3 * 4 + 2 * 4 * 2;
  const voxData = 2 * 3 * (3 + 4);
  const numBytes = v.readInt32LE(0);
  assert.equal(numBytes, 24 + offsetSize + voxData);
  assert.equal(v.length, 4 + numBytes + 768);
  assert.deepEqual([v.readInt32LE(4), v.readInt32LE(8), v.readInt32LE(12)], [2, 3, 4]);
  assert.equal(v.readInt32LE(28), offsetSize);
  assert.equal(v.readInt32LE(28 + 2 * 4) - offsetSize, voxData);
  const firstSlab = 4 + 24 + offsetSize;
  assert.deepEqual([...v.subarray(firstSlab, firstSlab + 4)], [0, 4, 63, 1]);
});

test("palette index 1 carries the colour", () => {
  const v = kvxBox(1, 1, 1, { color: [10, 20, 30] });
  const palette = v.subarray(v.length - 768);
  assert.deepEqual([...palette.subarray(3, 6)], [10, 20, 30]);
  assert.deepEqual([...palette.subarray(0, 3)], [0, 0, 0]);
});

test("a voxel lump sits between the voxel namespace markers", () => {
  const lumps = voxelLumps("COLU", Buffer.from([1]));
  assert.deepEqual(lumps.map((l) => l.name), ["VX_START", "COLU", "VX_END"]);
});
