import { test } from "node:test";
import assert from "node:assert/strict";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { buildHorizonWad } from "../scenarios/horizon.mjs";

// [rc4l] Read a PWAD's directory back, the way the engine will.
function lumpsOf(buf) {
  assert.equal(buf.toString("latin1", 0, 4), "PWAD");
  const n = buf.readInt32LE(4), dir = buf.readInt32LE(8);
  const out = [];
  for (let i = 0; i < n; i++) {
    const e = dir + 16 * i;
    const pos = buf.readInt32LE(e), size = buf.readInt32LE(e + 4);
    const name = buf.toString("latin1", e + 8, e + 16).replace(/\0+$/, "");
    out.push({ name, text: buf.toString("latin1", pos, pos + size) });
  }
  return out;
}

test("a position is written as a bare float, never quoted", () => {
  // [rc4l] The first template map quoted them, and the engine dropped the player start and crashed.
  const m = new UdmfMap();
  m.vertex(0, 0);
  m.thing({ x: 512, y: 724.5, angle: 90, type: 1 });
  const t = m.textmap();
  assert.match(t, /x = 0\.0;/);
  assert.match(t, /x = 512\.0;/);
  assert.match(t, /y = 724\.5;/);
  assert.doesNotMatch(t, /x = "/);
  assert.match(t, /angle = 90;/);
});

test("a room is wound so it is on every wall's right", () => {
  const m = new UdmfMap();
  m.room(0, 0, 64, 32, { heightfloor: 0 });
  const v = (i) => m.vertices[i];
  for (const l of m.lines) {
    const a = v(l.v1), b = v(l.v2);
    // [rc4l] The room's centre must lie on the right of a -> b, where cross(b - a, c - a) < 0.
    const cross = (b.x - a.x) * (16 - a.y) - (b.y - a.y) * (32 - a.x);
    assert.ok(cross < 0, `wall ${a.x},${a.y} -> ${b.x},${b.y} faces away from its room`);
  }
  assert.equal(m.vertices.length, 4, "corners are shared, not duplicated");
});

test("strings are quoted and booleans are bare", () => {
  const m = new UdmfMap();
  const s = m.sector({ texturefloor: "FLOOR4_8", heightfloor: 0 });
  m.wall(0, 0, 0, 64, s, { texturemiddle: "STARTAN2" }, { special: 9 });
  const t = m.textmap();
  assert.match(t, /texturefloor = "FLOOR4_8";/);
  assert.match(t, /texturemiddle = "STARTAN2";/);
  assert.match(t, /blocking = true;/);
  assert.match(t, /special = 9;/);
});

test("a wad holds its lumps in order with a directory that points at them", () => {
  const m = new UdmfMap();
  m.room(0, 0, 64, 64, { heightfloor: 0 });
  const lumps = lumpsOf(wad([...mapLumps("MAP01", m), { name: "longlumpname", data: "x" }]));
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "ENDMAP", "LONGLUMP"]);
  assert.equal(lumps[0].text, "");
  assert.match(lumps[1].text, /^namespace = "zdoom";/);
  assert.equal(lumps[3].text, "x");
});

test("the horizon template has a horizon line with a texture GL never draws, and a skybox in MAP02", () => {
  const lumps = lumpsOf(buildHorizonWad());
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "ENDMAP", "MAP02", "TEXTMAP", "ENDMAP"]);
  const [map01, map02] = [lumps[1].text, lumps[4].text];
  for (const t of [map01, map02]) {
    assert.match(t, /special = 9;/);
    assert.match(t, /texturemiddle = "STARTAN2";/);
    assert.match(t, /type = 1;/);
  }
  assert.doesNotMatch(map01, /type = 9080;/, "MAP01's sky is a texture");
  assert.match(map02, /type = 9080;/, "MAP02's sky is a place");
});
