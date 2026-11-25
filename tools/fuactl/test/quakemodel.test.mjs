import { test } from "node:test";
import assert from "node:assert/strict";
import { md2, md3 } from "../src/quakemodel.mjs";

// [Doruk-Aksoy] A triangle's two keyframes, the second raised.
const FRAMES = [[[0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 0, 2], [1, 0, 2], [0, 1, 3]]];

// [Doruk-Aksoy] The header FMD3Model::Load reads, the surface it walks to and the 1/64 vertices of the second frame.
test("md3 lays its header, surface and frames out as the loader reads them", () => {
  const b = md3({ frames: FRAMES, tris: [[0, 1, 2]], uvs: [[0, 0], [1, 0], [0, 1]], shader: "skin.png" });
  assert.equal(b.toString("latin1", 0, 4), "IDP3");
  assert.equal(b.readInt32LE(4), 15);
  assert.equal(b.readInt32LE(76), 2);   // frames
  assert.equal(b.readInt32LE(84), 1);   // surfaces
  const ofsSurf = b.readInt32LE(100);
  assert.equal(b.readInt32LE(104), b.length);
  assert.equal(b.toString("latin1", ofsSurf, ofsSurf + 4), "IDP3");
  assert.equal(b.readInt32LE(ofsSurf + 72), 2);
  assert.equal(b.readInt32LE(ofsSurf + 80), 3);   // vertices
  assert.equal(b.readInt32LE(ofsSurf + 84), 1);   // triangles
  const ofsTris = ofsSurf + b.readInt32LE(ofsSurf + 88);
  assert.deepEqual([0, 1, 2].map((c) => b.readInt32LE(ofsTris + c * 4)), [0, 1, 2]);
  assert.equal(b.toString("latin1", ofsSurf + b.readInt32LE(ofsSurf + 92), ofsSurf + b.readInt32LE(ofsSurf + 92) + 8), "skin.png");
  const ofsXyz = ofsSurf + b.readInt32LE(ofsSurf + 100);
  assert.equal(b.readInt16LE(ofsXyz + (3 + 2) * 8 + 4), 3 * 64);
  assert.equal(ofsSurf + b.readInt32LE(ofsSurf + 104), b.length);
  assert.equal(b.readFloatLE(108 + 56 + 20), 3);   // frame 1's max z
});

// [Doruk-Aksoy] The header FMD2Model::Load reads, a GL command so it loads, and each frame packed over its own box.
test("md2 lays its header, triangles and packed frames out as the loader reads them", () => {
  const b = md2({ frames: FRAMES, tris: [[0, 1, 2]], uvs: [[0, 0], [64, 0], [0, 64]] });
  assert.equal(b.toString("latin1", 0, 4), "IDP2");
  const h = (i) => b.readInt32LE(4 + i * 4);
  assert.equal(h(0), 8);
  assert.equal(h(5), 3);   // vertices
  assert.equal(h(7), 1);   // triangles
  assert.ok(h(8) > 0);     // GL commands
  assert.equal(h(9), 2);   // frames
  assert.equal(h(15), b.length);
  const ofsTris = h(12);
  assert.deepEqual([0, 1, 2, 3, 4, 5].map((c) => b.readInt16LE(ofsTris + c * 2)), [0, 1, 2, 0, 1, 2]);
  const f1 = h(13) + h(3);
  const scaleZ = b.readFloatLE(f1 + 8), lowZ = b.readFloatLE(f1 + 20);
  assert.equal(lowZ, 2);
  assert.ok(Math.abs(b.readUInt8(f1 + 40 + 2 * 4 + 2) * scaleZ + lowZ - 3) < 1e-6);
  assert.equal(b.readUInt8(f1 + 40 + 2), 0);
});

test("a frame of the wrong length is refused", () => {
  assert.throws(() => md3({ frames: [[[0, 0, 0]]], tris: [], uvs: [[0, 0], [1, 1]] }));
  assert.throws(() => md2({ frames: [[[0, 0, 0]], [[0, 0, 0], [1, 1, 1]]], tris: [], uvs: [] }));
});
