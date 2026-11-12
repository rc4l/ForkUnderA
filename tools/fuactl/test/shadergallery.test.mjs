// [Doruk-Aksoy] The shader gallery: blocks found by target through includes, the lumps they name resolved by path or short name, and a map per texture.
import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import zlib from "node:zlib";
import { pk3, readPk3, wad } from "../src/udmf.mjs";
import { readArchive, resolveLump, gldefsText, galleryBlocks, referencedNames, textureDefinitions, buildGallery } from "../src/shadergallery.mjs";

const file = (name, text) => ({ name, data: Buffer.from(text, "latin1") });

test("blocks are found by target in any case, postprocess ones and others left", () => {
  const text = `// HardwareShader texture HIDDEN { }
HardwareShader Texture "WALL1" { Shader "shaders/a.fp" Texture noise "textures/n.png" }
HardwareShader PostProcess scene { Name "x" }
Material flat FLOOR1 { normal NRM1 specular "SPC1" shader "shaders/b.fp" }
brightmap texture wall1 { map "BM1" }
pointlight L { color 1 0 0 }
Material texture OTHER { normal X }`;
  const blocks = galleryBlocks(text, ["wall1", "floor1", "hidden"]);
  assert.deepEqual(blocks.map((b) => [b.kind, b.target]), [["hardwareshader", "WALL1"], ["material", "FLOOR1"], ["brightmap", "wall1"]]);
  assert.match(blocks[1].text, /^Material flat FLOOR1 \{.*\}$/s);
  assert.deepEqual(referencedNames(blocks[0].text), ["shaders/a.fp", "textures/n.png"]);
  assert.deepEqual(referencedNames(blocks[1].text), ["NRM1", "SPC1", "shaders/b.fp"]);
  assert.deepEqual(referencedNames(blocks[2].text), ["BM1"]);
  assert.deepEqual(galleryBlocks("HardwareShader texture CUT { Shader", ["cut"]).map((b) => b.target), ["CUT"]);
  assert.deepEqual(galleryBlocks("Material", ["x"]), []);
});

test("a lump is found by its full path or its short name in a texture folder", () => {
  const files = [file("textures/Wall1.png", "w"), file("shaders/a.fp", "s"), file("sounds/wall2.ogg", "o"), file("graphics/BM1.png", "b")];
  assert.equal(resolveLump(files, "SHADERS/A.FP").name, "shaders/a.fp");
  assert.equal(resolveLump(files, "wall1").name, "textures/Wall1.png");
  assert.equal(resolveLump(files, "bm1").name, "graphics/BM1.png");
  assert.equal(resolveLump(files, "wall2"), null);
});

test("GLDEFS lumps at the root are read with their includes in place, once each", () => {
  const files = [file("gldefs.txt", 'A\n#include "lights/more.gl"\n#include "nosuch"\nB'), file("lights/more.gl", 'C #include "gldefs.txt"'), file("sub/gldefs.txt", "D")];
  assert.equal(gldefsText(files), "A\nC \n\nB");
});

test("a gallery holds each texture's blocks and lumps and a map apiece, naming what it could not find", () => {
  const files = [file("gldefs.txt", 'HardwareShader WALL1 { Shader "shaders/a.fp" }\nMaterial texture WALL2 { normal NRM }\nbrightmap texture WALL2 { map NRM }'),
    file("shaders/a.fp", "s"), file("textures/WALL1.png", "1"), file("textures/WALL2.png", "2")];
  const g = buildGallery(files, ["WALL1", "WALL2"], { extra: "fua_ProcessLighting uzdoom" });
  assert.deepEqual(g.maps, [{ map: "MAP01", texture: "WALL1" }, { map: "MAP02", texture: "WALL2" }]);
  assert.deepEqual(g.missing, ["NRM", "NRM"]);
  const names = readPk3(g.pk3).map((f) => f.name);
  for (const n of ["shaders/a.fp", "textures/WALL1.png", "textures/WALL2.png", "gldefs.txt", "maps/MAP01.wad", "maps/MAP02.wad"]) assert.ok(names.includes(n), n);
  const gldefs = readPk3(g.pk3).find((f) => f.name === "gldefs.txt").data.toString();
  assert.match(gldefs, /Shader "shaders\/a.fp" \tfua_ProcessLighting uzdoom\n\}/);
  assert.match(gldefs, /Material texture WALL2 \{ normal NRM \tfua_ProcessLighting uzdoom\n\}/);
  assert.match(gldefs, /brightmap texture WALL2 \{ map NRM \}/);
  assert.equal(buildGallery(files, ["WALL1"], { dark: true }).blocks.length, 1);
});

// [Doruk-Aksoy] A stored zip whose entries carry extra fields and comments, and the archive a comment, as 7-Zip and SLADE write them.
function zipWithExtras(entries, comment) {
  const locals = [], dir = [];
  let at = 0;
  for (const { name, data } of entries) {
    const n = Buffer.from(name, "latin1"), extra = Buffer.alloc(9, 1), note = Buffer.from("note", "latin1");
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt32LE(zlib.crc32(data), 14); local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(n.length, 26); local.writeUInt16LE(extra.length, 28);
    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0); central.writeUInt32LE(zlib.crc32(data), 16); central.writeUInt32LE(data.length, 20); central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(n.length, 28); central.writeUInt16LE(extra.length, 30); central.writeUInt16LE(note.length, 32); central.writeUInt32LE(at, 42);
    locals.push(local, n, extra, data);
    dir.push(central, n, extra, note);
    at += 30 + n.length + extra.length + data.length;
  }
  const d = Buffer.concat(dir), c = Buffer.from(comment, "latin1"), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(d.length, 12); end.writeUInt32LE(at, 16); end.writeUInt16LE(c.length, 20);
  return Buffer.concat([...locals, d, end, c]);
}

test("a texture TEXTURES defines comes with its definition and patches, and an include may be bare", () => {
  const files = [file("gldefs", "#include lights/more.gl"), file("lights/more.gl", "Material texture LWFLAT1 { normal NRM }"),
    file("TEXTURES.liquid", 'Texture "LWFLAT1", 64, 64\n{\n\tPatch "TEXTURES/MAP/LPWATER.PNG", 0, 0\n}\nTexture "OTHER", 8, 8 { Patch "X", 0, 0 }'),
    file("TEXTURES/MAP/LPWATER.PNG", "p"), file("textures/NRM.png", "n")];
  const defs = textureDefinitions(files, ["lwflat1"]);
  assert.deepEqual(defs.patches, ["TEXTURES/MAP/LPWATER.PNG"]);
  assert.deepEqual([...defs.defined], ["lwflat1"]);
  const g = buildGallery(files, ["LWFLAT1"]);
  assert.deepEqual(g.missing, []);
  const out = Object.fromEntries(readPk3(g.pk3).map((f) => [f.name, f.data.toString()]));
  assert.match(out["textures.txt"], /^Texture "LWFLAT1", 64, 64/);
  assert.ok(!out["textures.txt"].includes("OTHER"));
  assert.ok("TEXTURES/MAP/LPWATER.PNG" in out && "textures/NRM.png" in out);
});

test("a pk3 with extra fields and comments reads whole", () => {
  const z = zipWithExtras([{ name: "a.txt", data: Buffer.from("one") }, { name: "b/c.txt", data: Buffer.from("two") }], "made elsewhere");
  assert.deepEqual(readPk3(z).map((f) => [f.name, f.data.toString()]), [["a.txt", "one"], ["b/c.txt", "two"]]);
  assert.throws(() => readPk3(Buffer.alloc(40)), /no end of central directory/);
});

test("an archive is read as a pk3 or as a wad's lumps", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fua-gallery-"));
  const p = path.join(dir, "a.pk3"), w = path.join(dir, "a.wad");
  fs.writeFileSync(p, pk3([{ name: "gldefs.txt", data: "x" }]));
  fs.writeFileSync(w, wad([{ name: "GLDEFS", data: Buffer.from("y") }]));
  assert.equal(readArchive(p)[0].name, "gldefs.txt");
  assert.deepEqual(readArchive(w).map((f) => [f.name, f.data.toString()]), [["GLDEFS", "y"]]);
  fs.rmSync(dir, { recursive: true, force: true });
});
