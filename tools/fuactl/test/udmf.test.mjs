import { test } from "node:test";
import assert from "node:assert/strict";
import { UdmfMap, wad, mapLumps, readWad, parseUdmf, pk3, readPk3 } from "../src/udmf.mjs";
import { THING, SPECIAL } from "../src/doomnums.mjs";
import { PCD } from "../src/acs.mjs";
import { buildHorizonWad } from "../scenarios/horizon.mjs";
import { buildPointlightWad, VANTAGES } from "../scenarios/pointlight.mjs";

// [rc4l] A wad's lumps with their contents as text, which is how these tests read TEXTMAP and markers.
const lumpsOf = (buf) => readWad(buf).map((l) => ({ name: l.name, text: l.data.toString("latin1") }));

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

test("a map that runs scripts carries its BEHAVIOR between TEXTMAP and ENDMAP", () => {
  const names = mapLumps("MAP01", new UdmfMap(), { behavior: Buffer.from("ACS") }).map((l) => l.name);
  assert.deepEqual(names, ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
});

test("a block is a two-sided sector inside another, on every line's front, styled on the side facing out", () => {
  const m = new UdmfMap();
  const outer = m.room(0, 0, 256, 256, { heightfloor: 0 });
  const inner = m.block(64, 64, 192, 192, { heightfloor: 0, id: 7 }, outer, () => ({ texturebottom: "BRICK1" }),
    (edge) => ({ texturebottom: edge.toUpperCase() }));
  const lines = m.lines.slice(4);
  assert.equal(lines.length, 4);
  for (const l of lines) {
    const a = m.vertices[l.v1], b = m.vertices[l.v2];
    const cross = (b.x - a.x) * (128 - a.y) - (b.y - a.y) * (128 - a.x);
    assert.ok(cross < 0, `block line ${a.x},${a.y} -> ${b.x},${b.y} faces away from the block`);
    assert.equal(m.sides[l.sidefront].sector, inner);
    assert.match(m.sides[l.sidefront].texturebottom, /^(WEST|NORTH|EAST|SOUTH)$/);
    assert.deepEqual(m.sides[l.sideback], { sector: outer, texturebottom: "BRICK1" });
    assert.equal(l.twosided, true);
    assert.equal(l.blocking, undefined);
  }
});

test("a block's lines take their own properties per edge", () => {
  const m = new UdmfMap();
  const outer = m.room(0, 0, 256, 256, { heightfloor: 0 });
  m.block(64, 64, 192, 192, { heightfloor: 0 }, outer, () => ({}), () => ({}), (edge) => (edge === "west" ? { alpha: 0.5 } : {}));
  const lines = m.lines.slice(4);
  assert.deepEqual(lines.map((l) => l.alpha), [0.5, undefined, undefined, undefined]);
  assert.ok(lines.every((l) => l.twosided === true));
  assert.match(m.textmap(), /alpha = 0\.5;/);
});

test("a shape is a two-sided sector of any outline inside another, on every line's front", () => {
  const m = new UdmfMap();
  const outer = m.room(0, 0, 256, 256, { heightfloor: 0 });
  const inner = m.shape([[64, 64], [96, 192], [192, 160]], { heightfloor: 0 }, outer);
  const lines = m.lines.slice(4);
  assert.equal(lines.length, 3);
  for (const l of lines) {
    const a = m.vertices[l.v1], b = m.vertices[l.v2];
    const cross = (b.x - a.x) * (139 - a.y) - (b.y - a.y) * (117 - a.x);
    assert.ok(cross < 0, `shape line ${a.x},${a.y} -> ${b.x},${b.y} faces away from the shape`);
    assert.equal(m.sides[l.sidefront].sector, inner);
    assert.equal(m.sides[l.sideback].sector, outer);
  }
});

test("the frustum corner's patch lies past GL's estimate and inside what the corner sees, and its verdict wants the patch's colour", async () => {
  const { buildFrustumCornerWad, PATCH, judgeFrustumCorner } = await import("../scenarios/frustumcorner.mjs");
  assert.ok(buildFrustumCornerWad().length > 0);
  for (const [x, y] of PATCH) {
    const heading = Math.atan2(x, y) * 180 / Math.PI;
    assert.ok(heading > 71 && heading < 90, `patch point ${x},${y} at ${heading.toFixed(1)} degrees`);
  }
  const patch = { mean: 29, r: 0, g: 0, b: 86 };
  assert.equal(judgeFrustumCorner("x", { r: 2, g: 3, b: 80 }, patch).failed, 0);
  assert.match(judgeFrustumCorner("x", { r: 55, g: 55, b: 55 }, patch).line, /^FAIL x: the walk cut the corner/);
  assert.match(judgeFrustumCorner("x", patch, { mean: 3, r: 3, g: 3, b: 3 }).line, /^FAIL x: the patch is not drawn/);
});

test("a self-referencing square puts both sides of every line in its own new sector", () => {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0 });
  const hidden = m.selfRef(64, 64, 192, 192, { heightfloor: 48 });
  const lines = m.lines.slice(4);
  assert.equal(lines.length, 4);
  for (const l of lines) {
    assert.equal(m.sides[l.sidefront].sector, hidden);
    assert.equal(m.sides[l.sideback].sector, hidden);
    assert.equal(l.twosided, true);
  }
  assert.equal(m.sectors[hidden].heightfloor, 48);
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

test("the point-light template stands a white light above eye level, inside its radius of the wall every vantage faces", () => {
  const lumps = lumpsOf(buildPointlightWad());
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "ENDMAP"]);
  const t = lumps[1].text;
  assert.match(t, /type = 9800;/);
  assert.match(t, /height = 256;/);
  assert.match(t, /arg3 = 160;/);
  assert.match(t, /type = 1;/);
  // [rc4l] Every vantage looks north up the middle at the lit wall, from further back each time.
  const ys = VANTAGES.map((v) => v.at.y);
  assert.ok(VANTAGES.every((v) => v.at.x === 1024 && v.at.angle === 90));
  assert.deepEqual([...ys].sort((p, q) => q - p), ys);
});

// [Doruk-Aksoy] Each flag on its own actor's model, a top-only voxel, and MAP02 standing whole, whole and culled slabs where MAP01 stands the dont, plain and forced ones.
test("the model culling template flags one actor each way and stands each against a slab of known culling", async () => {
  const { buildModelCullFlagsPk3 } = await import("../scenarios/modelcullflags.mjs");
  const files = Object.fromEntries(readPk3(buildModelCullFlagsPk3()).map((f) => [f.name, f.data]));
  const modeldef = files["modeldef.txt"].toString("latin1"), decorate = files["decorate.txt"].toString("latin1");
  assert.match(modeldef, /Model ZXDont\n\{[^}]*\n  DONTCULLBACKFACES\n/);
  assert.match(modeldef, /Model ZXForced\n\{[^}]*\n  FORCECULLBACKFACES\n/);
  assert.doesNotMatch(modeldef, /Model ZX(Plain|Whole)\n\{[^}]*CULLBACKFACES/);
  assert.match(decorate, /Actor ZXDont 20100\n\{\n  RenderStyle Translucent/);
  assert.match(decorate, /Actor ZXForced 20102\n\{\n  RenderStyle Normal/);
  const kvx = files["models/flat.kvx"];
  assert.equal(kvx[4 + 24 + 17 * 4 + 16 * 17 * 2 + 2], 16, "only the top face is meshed");
  const typesOf = (name) => [...lumpsOf(files[name])[1].text.matchAll(/type = (\d+);/g)].map((t) => Number(t[1]));
  assert.deepEqual(typesOf("maps/MAP01.wad"), [20100, 20101, 20102, 1]);
  assert.deepEqual(typesOf("maps/MAP02.wad"), [20103, 20103, 20101, 1]);
});

// [Doruk-Aksoy] The pistol's model carries FORCECULLBACKFACES, and closed script 2 sets its puker's render style to ACS's Translucent.
test("the HUD force-cull template flags the pistol's model and gives MAP01 the script that makes the player translucent", async () => {
  const { buildHudForceCullPk3, TRANSLUCENT_PLAYER } = await import("../scenarios/hudforcecull.mjs");
  const files = Object.fromEntries(readPk3(buildHudForceCullPk3()).map((f) => [f.name, f.data]));
  assert.match(files["modeldef.txt"].toString("latin1"), /Model Pistol\n\{[^}]*\n  FORCECULLBACKFACES\n[^}]*FrameIndex PISG A/);
  const map = readWad(files["maps/MAP01.wad"]);
  assert.deepEqual(map.map((l) => l.name), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  assert.deepEqual(map[2].data, TRANSLUCENT_PLAYER);
  assert.deepEqual([3, 4, 5, 6].map((i) => TRANSLUCENT_PLAYER.readInt32LE(8 + 4 * i)), [4, PCD.PUSHNUMBER, 64, PCD.SETACTORPROPERTY]);
});

test("the sky templates carry what reproduces each fault: a scripted reflective pool, and a court too wide for the sky walls", async () => {
  const { buildSkymirrorWad } = await import("../scenarios/skymirror.mjs");
  const { buildSkyzenithWad } = await import("../scenarios/skyzenith.mjs");
  const mirror = lumpsOf(buildSkymirrorWad());
  assert.deepEqual(mirror.map((l) => l.name), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  assert.match(mirror[1].text, /id = 125;/);
  assert.match(mirror[1].text, /type = 9080;/);
  assert.equal(mirror[2].text.slice(0, 4), "ACS\0");
  const zenith = lumpsOf(buildSkyzenithWad())[1].text;
  assert.match(zenith, /x = 4096\.0;/);
  assert.match(zenith, /textureceiling = "F_SKY1";/);
});

test("a map reads back through parseUdmf with every value typed as it was written", () => {
  const m = new UdmfMap();
  m.room(0, 0, 64, 32, { heightfloor: 8, texturefloor: "FLOOR4_8" }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 16.5, y: 8, type: 1 });
  const u = parseUdmf(`// [rc4l] a comment the engine skips\n${m.textmap()}`);
  assert.equal(u.namespace, "zdoom");
  assert.deepEqual([u.vertex.length, u.linedef.length, u.sidedef.length, u.sector.length, u.thing.length], [4, 4, 4, 1, 1]);
  assert.deepEqual([u.sector[0].heightfloor, u.sector[0].texturefloor, u.linedef[0].blocking], [8, "FLOOR4_8", true]);
  assert.deepEqual([u.thing[0].x, u.thing[0].y], [16.5, 8]);
  assert.throws(() => readWad(Buffer.from("ZWAD....")), /not a wad/);
});

test("a skybox room is a 512 box with a wall texture per side and the default SkyViewpoint in its middle", () => {
  const m = new UdmfMap();
  m.skyboxRoom(4000, 4000);
  const u = parseUdmf(m.textmap());
  assert.deepEqual(u.sidedef.map((s) => s.texturemiddle), ["BROWN1", "COMPBLUE", "BRICK1", "STARTAN2"]);
  assert.deepEqual([u.thing[0].type, u.thing[0].x, u.thing[0].y, u.thing[0].height], [9080, 4256, 4256, 128]);
  assert.equal(u.sector[0].heightceiling, 384);
});

test("stacked rooms put a ceiling at 128 under a room dx east whose floor is at 128, with a shared-TID anchor in each", () => {
  const m = new UdmfMap();
  const { lower, upper } = m.stackedRooms({ dx: 2048, upperWidth: 1024, tid: 3 });
  const u = parseUdmf(m.textmap());
  assert.deepEqual([u.sector[lower].heightceiling, u.sector[upper].heightfloor, u.sector[upper].textureceiling], [128, 128, "F_SKY1"]);
  assert.deepEqual(u.vertex.map((v) => v.x).sort((a, b) => a - b).slice(-2), [3072, 3072]);
  assert.deepEqual(u.thing.map((t) => [t.type, t.x, t.id]), [[9078, 64, 3], [9077, 2112, 3]]);
});

test("the water template has a swimmable 3D floor on the pool's tag, and its colour set by script, by map, or not at all", async () => {
  const { buildWater3dWad } = await import("../scenarios/water3d.mjs");
  const lumps = lumpsOf(buildWater3dWad());
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP", "MAP02", "TEXTMAP", "ENDMAP",
    "MAP03", "TEXTMAP", "ENDMAP"]);
  for (const i of [1, 5, 8]) {
    assert.match(lumps[i].text, /special = 160;/);
    assert.match(lumps[i].text, /arg0 = 9;/);
    assert.match(lumps[i].text, /arg1 = 2;/);
  }
  assert.match(lumps[5].text, /lightcolor = 6598655;/);
  assert.doesNotMatch(lumps[8].text, /lightcolor/);
});

test("the warp template floors its pool with FWATER1 and makes that flat warp2 in an ANIMDEFS lump", async () => {
  const { buildWarpwaterWad } = await import("../scenarios/warpwater.mjs");
  const lumps = lumpsOf(buildWarpwaterWad());
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "ENDMAP", "ANIMDEFS"]);
  assert.match(lumps[1].text, /texturefloor = "FWATER1";/);
  assert.equal(lumps[3].text, "warp2 flat FWATER1\n");
});

test("the scroll-floor template's block carries the scrolling sector type and the camera stands off it", async () => {
  const { buildScrollFloorWad, VANTAGES: at } = await import("../scenarios/scrollfloor.mjs");
  const textmap = readWad(buildScrollFloorWad()).find((l) => l.name === "TEXTMAP").data.toString("latin1");
  const u = parseUdmf(textmap);
  assert.deepEqual(u.sector.map((s) => s.special ?? 0), [0, 206]);
  assert.ok(at[0].at.y < 512, "the camera must not stand on the scroller, which would carry it");
});

// [rc4l] Each new template's ingredient read back out of its own TEXTMAP, so a builder that drops it fails here rather than as a scenario that tests nothing.
const textmaps = (buf) => readWad(buf).filter((l) => l.name === "TEXTMAP").map((l) => parseUdmf(l.data.toString("latin1")));

test("the wall-light template transfers to the north wall by line id on MAP01 and lights it 255 outright on MAP02", async () => {
  const { buildWallLightWad } = await import("../scenarios/walllight.mjs");
  const buf = buildWallLightWad();
  const [transfer, fixed] = textmaps(buf);
  assert.ok(transfer.linedef.some((l) => l.special === 16 && l.arg0 === 1));
  assert.ok(transfer.linedef.some((l) => l.id === 1));
  assert.ok(readWad(buf).some((l) => l.name === "BEHAVIOR"));
  assert.ok(fixed.sidedef.some((s) => s.light === 255 && s.lightabsolute === true));
});

test("the raised 3D floor template makes its control room a 3D floor in the tagged block and raises it by script", async () => {
  const { buildRaised3DFloorWad } = await import("../scenarios/raised3dfloor.mjs");
  const buf = buildRaised3DFloorWad();
  const [u] = textmaps(buf);
  assert.ok(u.linedef.some((l) => l.special === 160 && l.arg0 === 3));
  assert.deepEqual(u.sector.filter((s) => s.id).map((s) => s.id), [3, 4]);
  assert.ok(readWad(buf).some((l) => l.name === "BEHAVIOR"));
});

test("the desaturation template writes 0.99 on MAP01 and an out-of-range 99 on MAP02", async () => {
  const { buildDesaturateWad } = await import("../scenarios/desaturate.mjs");
  assert.deepEqual(textmaps(buildDesaturateWad()).map((u) => u.sector[0].desaturation), [0.99, 99]);
});

test("the mirror-behind template's pillar stands on its own block behind the player, facing a mirror", async () => {
  const { buildMirrorBehindWad, VANTAGES: at } = await import("../scenarios/mirrorbehind.mjs");
  const [u] = textmaps(buildMirrorBehindWad());
  assert.ok(u.linedef.some((l) => l.special === 182));
  assert.equal(u.sector.length, 2);
  const pillar = u.thing.find((t) => t.type === 48);
  assert.ok(pillar.y < at[0].at.y, "the pillar must be behind the player");
});

test("an opening is one two-sided line with its front sector on the right, each side styled by its own argument", () => {
  const m = new UdmfMap();
  const a = m.sector({}), b = m.sector({});
  const l = m.opening(0, 0, 64, 0, a, b, { texturebottom: "BROWN1" }, { texturetop: "STARTAN2" }, { special: 9 });
  assert.equal(l, 0);
  assert.deepEqual(m.lines[0], { v1: 0, v2: 1, sidefront: 0, sideback: 1, twosided: true, special: 9 });
  assert.deepEqual(m.sides, [{ sector: a, texturebottom: "BROWN1" }, { sector: b, texturetop: "STARTAN2" }]);
});

test("the mirror-sky template's yard is shallower than the player stands from the mirror, with a low sky beyond the window beside it", async () => {
  const { buildMirrorSkyWad, VANTAGES: at } = await import("../scenarios/mirrorsky.mjs");
  const [u] = textmaps(buildMirrorSkyWad());
  const mirror = u.linedef.find((l) => l.special === 182);
  assert.ok(mirror);
  const yard = u.sector.find((s) => s.textureceiling === "F_SKY1");
  assert.ok(yard.heightceiling < u.sector[0].heightceiling, "the yard's sky must sit below the room's ceiling");
  const yardDepth = 1024 - 528, mirrorY = u.vertex[mirror.v1].y;
  assert.ok(mirrorY - at[0].at.y > yardDepth, "the reflected eye must stand beyond the yard's far wall");
  assert.equal(u.linedef.filter((l) => l.twosided).length, 2, "the window and its far edge");
});

test("the sky-wall-below template's closed strip is a sky sector shut below eye level between the player and a room the walk never reaches", async () => {
  const { buildSkyWallBelowWad } = await import("../scenarios/skywallbelow.mjs");
  const [u] = textmaps(buildSkyWallBelowWad());
  const closed = u.sector.find((s) => s.heightfloor === s.heightceiling);
  assert.equal(closed.textureceiling, "F_SKY1");
  assert.ok(closed.heightceiling > 0 && closed.heightceiling < 41, "above the floor, below the eye");
  assert.equal(u.linedef.filter((l) => l.twosided).length, 2);
  assert.ok(u.thing.some((t) => t.type === 9080), "a skybox for the sky wall to show");
});

test("the deep horizon template stands the player 256 over the court, and only its oblique vantage judges the band", async () => {
  const { buildHorizonDeepWad, VANTAGES: at, BOXES } = await import("../scenarios/horizondeep.mjs");
  const [u] = textmaps(buildHorizonDeepWad());
  assert.ok(u.linedef.some((l) => l.special === 9));
  assert.equal(u.sector[1].heightfloor - u.sector[0].heightfloor, 256);
  assert.equal(BOXES.MAP01.band.vantage, "oblique");
  assert.ok(at.find((v) => v.name === "oblique").at.angle !== 90, "square-on, the depth fight never shows");
});

test("the open skybox template's viewpoint stands in a room of sky and horizon lines, under a six-face GLDEFS skybox", async () => {
  const { buildSkyboxOpenWad } = await import("../scenarios/skyboxopen.mjs");
  const buf = buildSkyboxOpenWad();
  const lumps = lumpsOf(buf);
  assert.match(lumps.find((l) => l.name === "GLDEFS").text, /skybox OBOX/);
  assert.match(lumps.find((l) => l.name === "MAPINFO").text, /sky1 = "OBOX"/);
  const names = lumps.map((l) => l.name);
  assert.equal(names.indexOf("TX_END") - names.indexOf("TX_START"), 7, "six faces between the markers");
  const [u] = textmaps(buf);
  const box = u.sector.find((s) => s.texturefloor === "F_SKY1");
  assert.equal(box.textureceiling, "F_SKY1");
  assert.equal(u.linedef.filter((l) => l.special === 9).length, 4);
  assert.ok(u.thing.some((t) => t.type === 9080));
});

test("the water-tint template's additive water is tinted by closed script 2, which each side pukes once its level is up", async () => {
  const { buildWaterTintWad, SIDES } = await import("../scenarios/watertint.mjs");
  const buf = buildWaterTintWad();
  const [u] = textmaps(buf);
  const water = u.linedef.find((l) => l.special === 160);
  assert.deepEqual([water.arg2, water.arg3], [64, 128]);
  const behavior = readWad(buf).find((l) => l.name === "BEHAVIOR").data;
  assert.equal(behavior.readInt32LE(behavior.readInt32LE(4) + 4), 2, "closed, so it runs only when puked");
  assert.ok(SIDES.length === 2 && SIDES.every((s) => s.commands.includes("puke 2")));
});

test("the inverted-back template's strip has its floor above its ceiling, which meets the room's ceiling, and a red wall behind", async () => {
  const { buildInvertedBackWad, SIDES } = await import("../scenarios/invertedback.mjs");
  const [u] = textmaps(buildInvertedBackWad());
  const [room, inverted] = u.sector;
  assert.ok(inverted.heightfloor > inverted.heightceiling, "floor above ceiling");
  assert.equal(inverted.heightfloor, room.heightceiling, "so GL draws no top on the wall between them");
  assert.ok(u.sidedef.some((s) => s.texturemiddle === "REDWALL"));
  assert.deepEqual(SIDES.map((s) => s.frame || (s.gl ? "gl alone" : "vk")), ["gl alone", "vk"]);
});

test("the missing-lower template sinks an untextured recess into the floor and looks at a red wall before it", async () => {
  const { buildMissingLowerWad, VANTAGES: at, BOXES } = await import("../scenarios/missinglower.mjs");
  const [u] = textmaps(buildMissingLowerWad());
  assert.ok(u.sector[1].heightfloor < u.sector[0].heightfloor, "a recess");
  assert.ok(u.sidedef.filter((s) => s.sector === 1).every((s) => !s.texturebottom), "its steps have no lower texture");
  assert.deepEqual(at.map((v) => v.name), ["red", "down"], "stale red must be in GL's framebuffer before the steps are judged");
  assert.equal(BOXES.MAP01.steps.vantage, "down");
});

test("the missing-upper template raises an untextured island's ceiling over the room and stands inside it looking up", async () => {
  const { buildMissingUpperWad, VANTAGES: at, SIDES } = await import("../scenarios/missingupper.mjs");
  const [u] = textmaps(buildMissingUpperWad());
  const [room, island] = u.sector;
  assert.ok(island.heightceiling > room.heightceiling, "the island's ceiling is above the room's");
  assert.ok(u.sidedef.filter((s) => s.sector === 1).every((s) => !s.texturetop), "and no upper closes the step");
  assert.ok(at[0].at.x > 256 && at[0].at.x < 768 && at[0].at.y > 256 && at[0].at.y < 768 && at[0].at.pitch < 0);
  assert.equal(SIDES[0].cvars.gl_wallcache, 0, "stock GL's fill as the reference");
});

test("the fill-cache template compares GL with its wall cache off and on over the missing-upper map", async () => {
  const { SIDES } = await import("../scenarios/missingfillcache.mjs");
  assert.ok(SIDES.every((s) => s.gl));
  assert.deepEqual(SIDES.map((s) => (s.cvars || {}).gl_wallcache), [0, undefined]);
});

test("the sky-hole template leaves the yard's north wall bare under a sky, with a lit room behind it that nothing joins", async () => {
  const { buildSkyHoleWad } = await import("../scenarios/skyhole.mjs");
  const [u] = textmaps(buildSkyHoleWad());
  const bare = u.linedef.filter((l) => l.sideback === undefined && !u.sidedef[l.sidefront].texturemiddle);
  assert.equal(bare.length, 1, "one bare one-sided wall");
  assert.equal(u.sector[u.sidedef[bare[0].sidefront].sector].textureceiling, "F_SKY1");
  assert.ok(u.linedef.every((l) => !l.twosided), "nothing joins the room behind it");
});

test("the sky-hole template's pillar maps keep the field beyond the bare wall in the yard's own sector, one with a skybox and one without", async () => {
  const { buildSkyHoleWad } = await import("../scenarios/skyhole.mjs");
  const [, portal, dome] = textmaps(buildSkyHoleWad());
  for (const u of [portal, dome]) {
    assert.equal(u.sector.length - (u === portal ? 1 : 0), 1, "the yard, the corridor and the field are one sector");
    const bare = u.linedef.filter((l) => !u.sidedef[l.sidefront].texturemiddle);
    assert.equal(bare.length, 1, "one bare one-sided wall");
    const pillar = u.thing.find((t) => t.type === THING.TechPillar);
    assert.ok(pillar && pillar.y > u.vertex[bare[0].v1].y, "the pillar stands beyond the bare wall");
  }
  assert.ok(portal.thing.some((t) => t.type === THING.SkyViewpoint));
  assert.ok(!dome.thing.some((t) => t.type === THING.SkyViewpoint));
});

test("the camera-texture template declares a wide camera texture fitted smaller, shows it on the screen wall and aims a tagged camera at it from an OPEN script", async () => {
  const { buildCameraTexWad } = await import("../scenarios/cameratex.mjs");
  const lumps = lumpsOf(buildCameraTexWad());
  const animdefs = lumps.find((l) => l.name === "ANIMDEFS").text;
  const [, w, h, fw, fh] = animdefs.match(/cameratexture FUACAM (\d+) (\d+) fit (\d+) (\d+)/).map(Number);
  assert.ok(w > h && fw < w, "wider than tall, fitted smaller than its pixels");
  assert.equal(fw / fh, w / h);
  const [u] = textmaps(buildCameraTexWad());
  assert.equal(u.sidedef.filter((s) => s.texturemiddle === "FUACAM").length, 1);
  const camera = u.thing.find((t) => t.type === 9001);
  const behavior = readWad(buildCameraTexWad()).find((l) => l.name === "BEHAVIOR").data;
  const words = [];
  for (let i = 8; i + 4 <= behavior.readInt32LE(4); i += 4) words.push(behavior.readInt32LE(i));
  assert.deepEqual(words.slice(0, 7), [PCD.PUSHNUMBER, camera.id, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE]);
});

test("the sprite-slot template stands animated decorations both north and south of the player, so turning round shows frames never drawn", async () => {
  const { buildSpriteSlotsWad, VANTAGES } = await import("../scenarios/spriteslots.mjs");
  const [u] = textmaps(buildSpriteSlotsWad());
  const start = u.thing.find((t) => t.type === 1);
  const decorations = u.thing.filter((t) => t.type !== 1);
  assert.ok(decorations.some((t) => t.y > start.y) && decorations.some((t) => t.y < start.y));
  assert.deepEqual(VANTAGES.map((v) => v.at.angle), [90, 270]);
});

test("the skybox-beyond template puts a tower taller than the skybox area's walls in a sealed room beside it", async () => {
  const { buildSkyboxBeyondWad } = await import("../scenarios/skyboxbeyond.mjs");
  const [u] = textmaps(buildSkyboxBeyondWad());
  const viewpoint = u.thing.find((t) => t.type === THING.SkyViewpoint);
  const tower = u.sector.find((s) => s.heightfloor >= 1000);
  const skyArea = u.sector.find((s) => s.texturefloor === "FLOOR0_1");
  assert.ok(viewpoint && tower && skyArea);
  assert.ok(tower.heightfloor > skyArea.heightceiling, "the tower rises over the skybox walls");
});

test("the transfer-heights template tags its lake from a control sector's Transfer_Heights line, the surface above the lake's own floor", async () => {
  const { buildTransferHeightsWad } = await import("../scenarios/transferheights.mjs");
  const [u] = textmaps(buildTransferHeightsWad());
  const line = u.linedef.find((l) => l.special === SPECIAL.Transfer_Heights);
  const lake = u.sector.find((s) => s.id === line.arg0);
  const control = u.sector[u.sidedef[line.sidefront].sector];
  assert.ok(control.heightfloor > lake.heightfloor);
  assert.ok(control.lightcolor > 0);
});

test("a pk3 reads back as the files it was written from, in order", () => {
  const files = [{ name: "maps/MAP01.wad", data: wad([{ name: "MAP01", data: "" }]) }, { name: "modeldef.txt", data: "Model Pistol { }" },
    { name: "models/empty.kvx", data: "" }];
  const back = readPk3(pk3(files));
  assert.deepEqual(back.map((f) => f.name), files.map((f) => f.name));
  assert.equal(back[1].data.toString("latin1"), "Model Pistol { }");
  assert.equal(back[2].data.length, 0);
  assert.deepEqual(readWad(back[0].data).map((l) => l.name), ["MAP01"]);
});

test("a pk3 that is not one, or whose file was changed, is refused", () => {
  assert.throws(() => readPk3(Buffer.from("PWAD")), /not a pk3/);
  const buf = pk3([{ name: "a.txt", data: "abc" }]);
  buf[30 + "a.txt".length] ^= 1;
  assert.throws(() => readPk3(buf), /a\.txt: CRC mismatch/);
});

test("a deflated pk3 reads back inflated, and a compression method it cannot read is refused by name", () => {
  const files = [{ name: "music/a.wav", data: Buffer.alloc(4096, 0x80) }, { name: "mapinfo.txt", data: "map MAP01 {}" }, { name: "empty", data: "" }];
  const buf = pk3(files, { deflate: true });
  assert.ok(buf.length < 4096, "silence deflates to far less than it holds");
  const back = readPk3(buf);
  assert.deepEqual(back.map((f) => [f.name, f.data.length, f.deflated]), [["music/a.wav", 4096, true], ["mapinfo.txt", 12, true], ["empty", 0, true]]);
  assert.equal(back[1].data.toString("latin1"), "map MAP01 {}");
  assert.equal(readPk3(pk3(files))[0].deflated, undefined);
  const odd = Buffer.from(buf);
  odd.writeUInt16LE(14, odd.readUInt32LE(odd.length - 22 + 16) + 10);
  assert.throws(() => readPk3(odd), /music\/a\.wav: compression method 14, not stored or deflated/);
});

test("the wipe scenario reads fua_wipe_report and fails a wipe never presented, a cut, a wrong start or end, and the old map before the second", async () => {
  const { buildWipeWad, parseWipeReport, wipeFault, judgeWipes } = await import("../scenarios/wipe.mjs");
  assert.ok(buildWipeWad().length > 0);
  assert.equal(parseWipeReport("Unknown command"), null);
  const melt = (span) => ({ frames: 10, span, sinceLast: -1, toBefore: [1, 10, 20, 30, 40, 50, 60, 70, 80, 90], toAfter: [90, 80, 70, 60, 50, 40, 30, 20, 10, 1] });
  const text = "wipes started 2, presented 2\nwipe 1: 3 frames, before to after 50.00, before to the last wipe's after -1.00\n  frame 1: 2.00 from before, 48.00 from after\n  frame 2: 25.00 from before, 25.00 from after\n"
    + "  frame 3: 49.00 from before, 1.00 from after\nwipe 2: 0 frames, before to after 40.00, before to the last wipe's after 3.50\n";
  const r = parseWipeReport(text);
  assert.deepEqual([r.started, r.presented, r.wipes.length], [2, 2, 2]);
  assert.deepEqual(r.wipes[0], { frames: 3, span: 50, sinceLast: -1, toBefore: [2, 25, 49], toAfter: [48, 25, 1] });
  assert.equal(r.wipes[1].sinceLast, 3.5);
  assert.equal(wipeFault(melt(100)), null);
  assert.match(wipeFault({ ...melt(100), frames: 3 }), /3 frames presented/);
  assert.match(wipeFault(melt(2)), /two ends look alike/);
  assert.match(wipeFault({ ...melt(100), toBefore: [60, ...melt(100).toBefore.slice(1)] }), /did not start there/);
  assert.match(wipeFault({ ...melt(100), toAfter: [...melt(100).toAfter.slice(0, 9), 60] }), /did not end there/);
  assert.match(wipeFault({ ...melt(100), toBefore: [1, 1, 1, 1, 1, 99, 99, 99, 99, 99], toAfter: [99, 99, 99, 99, 99, 1, 1, 1, 1, 1] }), /cut rather than melted/);
  const good = { started: 2, presented: 2, wipes: [melt(100), { ...melt(80), sinceLast: 10 }] };
  assert.equal(judgeWipes("x", good).failed, 0);
  assert.match(judgeWipes("x", good).line, /^PASS x: wipe 1 10 frames, wipe 2 10 frames$/);
  assert.match(judgeWipes("x", null).line, /no fua_wipe_report answer/);
  assert.match(judgeWipes("x", { started: 2, presented: 0, wipes: [] }).line, /2 of 2 wipes never reached the screen/);
  assert.match(judgeWipes("x", { started: 1, presented: 1, wipes: [melt(100)] }).line, /1 wipes began/);
  assert.match(judgeWipes("x", { ...good, wipes: [melt(100), { ...melt(80), sinceLast: 70 }] }).line, /the old map showed through/);
});

test("the command-safety template ships a script and both menu kinds trying atexit, and its verdict wants every route refused and writeini beside the config", async () => {
  const { buildCommandSafetyWad, judgeCommandSafetyFrom } = await import("../scenarios/commandsafety.mjs");
  const lumps = readWad(buildCommandSafetyWad());
  const behavior = lumps.find((l) => l.name === "BEHAVIOR").data.toString("latin1");
  const menudef = lumps.find((l) => l.name === "MENUDEF").data.toString("latin1");
  assert.match(behavior, /atexit "echo fua-script-ran"/);
  assert.match(menudef, /SafeCommand "Probe", "atexit \\"echo fua-option-ran\\""/);
  assert.match(menudef, /FreeformMenu "FuaSafetyFree"[\s\S]*Command "atexit \\"echo fua-freeform-ran\\""/);
  const saved = { file: "/cfg/---fua-commandsafety.ini", exists: true };
  assert.equal(judgeCommandSafetyFrom("x", { listed: "Registered atexit commands:\n", refusals: 2, saved }).failed, 0);
  assert.match(judgeCommandSafetyFrom("x", { listed: "echo fua-option-ran\n", refusals: 2, saved }).line, /the option registered an atexit command/);
  assert.match(judgeCommandSafetyFrom("x", { listed: "", refusals: 1, saved }).line, /1 of the 2 menu items were refused/);
  assert.match(judgeCommandSafetyFrom("x", { listed: "", refusals: 2, saved: null }).line, /writeini named no file/);
  assert.match(judgeCommandSafetyFrom("x", { listed: "", refusals: 2, saved: { file: "../fua-commandsafety", exists: true } }).line, /writeini wrote \.\.\/fua-commandsafety/);
  assert.match(judgeCommandSafetyFrom("x", { listed: "", refusals: 2, saved: { ...saved, exists: false } }).line, /which is not there/);
});
