import { test } from "node:test";
import assert from "node:assert/strict";
import zlib from "node:zlib";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { readZip, findMapLumps, mapReport, grepScripts } from "../src/mapdata.mjs";

// [rc4l] A zip of { name: Buffer } entries, deflating the ones named in `deflate`, laid out as a pk3 tool writes one.
function zip(entries, deflate = []) {
  const locals = [], centrals = [];
  let offset = 0;
  for (const [name, data] of Object.entries(entries)) {
    const method = deflate.includes(name) ? 8 : 0;
    const body = method === 8 ? zlib.deflateRawSync(data) : data;
    const n = Buffer.from(name, "latin1");
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(method, 8);
    local.writeUInt32LE(body.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(n.length, 26);
    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(method, 10);
    central.writeUInt32LE(body.length, 20); central.writeUInt32LE(data.length, 24); central.writeUInt16LE(n.length, 28);
    central.writeUInt32LE(offset, 42);
    locals.push(local, n, body);
    centrals.push(central, n);
    offset += 30 + n.length + body.length;
  }
  const dir = Buffer.concat(centrals);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0); eocd.writeUInt16LE(Object.keys(entries).length, 10);
  eocd.writeUInt32LE(dir.length, 12); eocd.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, dir, eocd]);
}

// [rc4l] A two-room map: a sky court with a special on its north wall, and a closed room beside it.
function courtMap() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0, heightceiling: 128, textureceiling: "F_SKY1" }, (edge) =>
    (edge === "north" ? { line: { special: 9 } } : {}));
  m.room(512, 0, 768, 256, { heightfloor: 0, heightceiling: 96, textureceiling: "CEIL3_5" });
  m.thing({ x: 128, y: 128, type: 1 });
  return m;
}

test("a pk3's entries read back whether stored or deflated", () => {
  const entries = readZip(zip({ "a.txt": Buffer.from("stored"), "maps/b.wad": Buffer.from("squeezed squeezed") }, ["maps/b.wad"]));
  assert.deepEqual([...entries.keys()], ["a.txt", "maps/b.wad"]);
  assert.equal(entries.get("a.txt")().toString(), "stored");
  assert.equal(entries.get("maps/b.wad")().toString(), "squeezed squeezed");
  assert.throws(() => readZip(Buffer.from("not a zip at all, not even close to one")), /not a zip/);
});

test("a map is found in a wad or in a pk3's maps/ folder, and the last file holding it wins", () => {
  const plain = wad(mapLumps("MAP01", courtMap()));
  // [rc4l] Inside a pk3 the map wad's own marker can be named anything.
  const pk3 = zip({ "maps/MAP01.wad": wad([...mapLumps("WHATEVER", courtMap()), { name: "SCRIPTS", data: "" }]) }, ["maps/MAP01.wad"]);
  assert.equal(findMapLumps([{ name: "a.wad", buf: plain }], "map01").file, "a.wad");
  const both = findMapLumps([{ name: "a.wad", buf: plain }, { name: "b.pk3", buf: pk3 }], "MAP01");
  assert.equal(both.file, "b.pk3");
  assert.deepEqual([...both.lumps.keys()], ["TEXTMAP", "ENDMAP", "SCRIPTS"]);
  assert.equal(findMapLumps([{ name: "a.wad", buf: plain }], "MAP02"), null);
});

test("the report summarises the map and describes the sectors and lines asked about", () => {
  const lumps = [...mapLumps("MAP01", courtMap()).slice(0, 2),
    { name: "SCRIPTS", data: "script 1 OPEN\n{\n  Sector_SetPlaneReflection(125, 66, 0);\n}\n" }, { name: "ENDMAP", data: "" }];
  const found = findMapLumps([{ name: "a.wad", buf: wad(lumps) }], "MAP01");
  const r = mapReport(found, { sectors: [0], lines: [1], scripts: "reflection" });
  assert.deepEqual(r.summary, { namespace: "zdoom", sectors: 2, lines: 8, things: 1, specials: { 9: 1 }, skyCeilings: 1, skyFloors: 0 });
  assert.equal(r.sectors[0].textureceiling, "F_SKY1");
  assert.deepEqual(r.sectors[0].lines, [0, 1, 2, 3]);
  assert.deepEqual([r.lines[0].from, r.lines[0].to, r.lines[0].front, r.lines[0].back, r.lines[0].special], [[0, 256], [256, 256], 0, null, 9]);
  assert.deepEqual(r.scripts, [{ line: 3, text: "Sector_SetPlaneReflection(125, 66, 0);" }]);
  assert.throws(() => mapReport(found, { sectors: [5] }), /no sector 5: the map has 2/);
  assert.throws(() => mapReport(found, { lines: [99] }), /no line 99: the map has 8/);
});

test("a map with no script source says so, and a binary-format map is refused by name", () => {
  const found = findMapLumps([{ name: "a.wad", buf: wad(mapLumps("MAP01", courtMap())) }], "MAP01");
  assert.equal(mapReport(found, { scripts: "x" }).scripts, "no SCRIPTS source in the map");
  assert.equal(mapReport(found).scripts, undefined);
  const binary = findMapLumps([{ name: "old.wad", buf: wad([{ name: "MAP01", data: "" }, { name: "THINGS", data: "x" }]) }], "MAP01");
  assert.throws(() => mapReport(binary), /old\.wad holds the map in binary format/);
  assert.deepEqual(grepScripts("a\r\nB\r\nab", "^a"), [{ line: 1, text: "a" }, { line: 3, text: "ab" }]);
});
