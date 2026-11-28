import { test } from "node:test";
import assert from "node:assert/strict";
import { colourise, tint, expectedColours, acsTranslation, squarePoints, patchColour, judgePicture, judgeTables, buildRangesPk3, buildDecorate, buildScript,
  SQUARES, COLOURISE, TINT, REFUSED, REDEFINED, TRNSLATE, UZDOOM_CHAIN, PICTURE_TOLERANCE, iwadPalette } from "../scenarios/acsranges.mjs";
import { STEPS, LATE, judgeMachine, judgeNetplay, buildNetRangesPk3, SEEN_SCRIPT } from "../scenarios/netacsranges.mjs";
import { PCD } from "../src/acs.mjs";
import { doomPicture } from "../src/udmf.mjs";

// [Doruk-Aksoy] The same values the engine's TrnslateRanges unit tests pin, worked out by hand from UZDoom's formulas.
test("colourise and tint give UZDoom's bytes, wrapping past 255", () => {
  assert.deepEqual(colourise([100, 0, 0], [200, 100, 50]), [23, 11, 5]);
  assert.deepEqual(colourise([255, 255, 255], [300, -50, 1000]), [44, 206, 232]);
  assert.deepEqual(colourise([255, 255, 255], [2147483647 * 2, 0, 0]), [255, 0, 0]);
  assert.deepEqual(tint([100, 50, 0], [200, 100, 50], 25), [125, 62, 12]);
  assert.deepEqual(tint([255, 255, 255], [0, 0, 0], 33), [170, 170, 170]);
  assert.deepEqual(tint([10, 10, 10], [-50, 0, 0], 100), [206, 0, 0]);
});

test("a range's expected colours leave the end out and follow the palette's order", () => {
  const base = Array.from({ length: 8 }, (_, i) => [i, i, i]);
  const order = [0, 1, 2, 2, 4, 5, 6, 7];
  const got = expectedColours(base, order, { start: 1, end: 4 }, ([v]) => [v * 10, 0, 0]);
  assert.deepEqual(got[1], [10, 0, 0]);
  assert.deepEqual(got[2], [30, 0, 0], "index 3 shares index 2's entry and comes later");
  assert.deepEqual(got[4], [4, 4, 4], "the end is left out");
  assert.equal(acsTranslation(1), 4 << 16);
  assert.equal(acsTranslation(3), (4 << 16) | 2);
});

test("the pk3 holds the map, the squares, both TRNSLATE forms and a 32x32 picture per palette index", () => {
  assert.match(TRNSLATE, /ZxColourise = "16:48=#\[255,128,0\]"/);
  assert.match(TRNSLATE, /ZxTint = "16:48=@40\[0,64,255\]"/);
  assert.match(buildDecorate(), /actor ZxSqNamedTint 30304[\s\S]*Translation "ZxTint"[\s\S]*ZXSQ C -1 Bright/);
  const words = buildScript();
  for (const op of [PCD.TRANSLATIONRANGE4, PCD.TRANSLATIONRANGE5, PCD.TRANSLATIONRANGE1]) assert.ok(words.includes(op));
  const text = buildRangesPk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "decorate.txt", "trnslate.txt", "sprites/ZXSQA0.lmp", "sprites/ZXSQB0.lmp", "sprites/ZXSQC0.lmp"]) assert.ok(text.includes(name), name);
  const pic = doomPicture(2, 3, 7);
  assert.deepEqual([pic.readUInt16LE(0), pic.readUInt16LE(2), pic.readInt16LE(4), pic.readInt16LE(6)], [2, 3, 1, 3]);
  assert.deepEqual([...pic.subarray(16, 24)], [0, 3, 0, 7, 7, 7, 0, 0xff]);
  assert.equal(pic.readUInt32LE(12), 24);
  assert.match(UZDOOM_CHAIN, /crosshair 0;puke 1;.*screenshot/);
});

test("the squares fall across a widened view and a picture is judged by each one's patch", () => {
  assert.deepEqual(squarePoints(1024, 640).map(([x]) => x), [192, 352, 512, 672, 832]);
  assert.deepEqual(squarePoints(640, 480)[0], [80, 240]);
  const base = iwadPalette();
  const points = squarePoints(64, 40);
  const colours = SQUARES.map((s) => s.want(base));
  const img = { w: 64, h: 40, channels: 3, colorType: 2, data: Buffer.alloc(64 * 40 * 3) };
  points.forEach(([x, y], i) => {
    for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) colours[i].forEach((v, k) => { img.data[((y + dy) * 64 + x + dx) * 3 + k] = v; });
  });
  assert.deepEqual(patchColour(img, points[0]), colours[0]);
  assert.deepEqual(judgePicture(img, base), []);
  img.data[(points[2][1] * 64 + points[2][0]) * 3] = 0;
  assert.deepEqual(judgePicture(img, base), [], "one stray pixel can't decide a patch");
  for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) img.data[((points[2][1] + dy) * 64 + points[2][0] + dx) * 3] = colours[2][0] + PICTURE_TOLERANCE + 1;
  assert.match(judgePicture(img, base).join(), /ZxSqTint drew/);
});

// [Doruk-Aksoy] A fake world.translation over a palette of greys in order, each table built as the engine would.
function fakeTables({ broken = null, missing = null } = {}) {
  const base = Array.from({ length: 256 }, (_, i) => (i << 16) | (i << 8) | i);
  const order = base.map((_, i) => i);
  const rgb = base.map((v) => [v & 0xff, v & 0xff, v & 0xff]);
  const table = (range, f) => ({ base, order, remap: order.slice(), palette: expectedColours(rgb, order, range, f).map(([r, g, b]) => (r << 16) | (g << 8) | b) });
  const col = table(COLOURISE, (c) => colourise(c, COLOURISE.rgb)), tin = table(TINT, (c) => tint(c, TINT.rgb, TINT.amount));
  if (broken === true) col.palette[COLOURISE.end] = 1;
  const refused = { base, order, remap: order.slice(), palette: base.slice() };
  const redefined = { base, order, remap: order.slice(), palette: broken === REDEFINED ? col.palette.slice() : base.slice() };
  return async ({ translation, name }) => {
    if (missing && (name === missing || translation === missing)) return null;
    if (name) return name === COLOURISE.name ? col : tin;
    return { [acsTranslation(COLOURISE.acs)]: col, [acsTranslation(TINT.acs)]: tin, [acsTranslation(REFUSED)]: refused, [acsTranslation(REDEFINED)]: redefined }[translation];
  };
}

test("ours' tables are judged entry by entry, the TRNSLATE twins against the ACS ones, the refused one untouched", async () => {
  assert.deepEqual(await judgeTables(fakeTables()), []);
  assert.match((await judgeTables(fakeTables({ broken: true }))).join(), /ACS translation 1 has 1 entries off UZDoom's formula: 48/);
  assert.match((await judgeTables(fakeTables({ missing: COLOURISE.name }))).join(), /ZxColourise reads nothing/);
  assert.match((await judgeTables(fakeTables({ missing: acsTranslation(REFUSED) }))).join(), /ACS translation 3 reads nothing/);
  assert.match((await judgeTables(fakeTables({ broken: REDEFINED }))).join(), /ACS translation 4 is not the identity/);
});

test("the netgame builds on the server, every client then reads, the late joiner too", async () => {
  assert.deepEqual(STEPS.map((s) => [s.script, s.clients !== false, s.server !== false]), [[1, false, true], [2, true, false]]);
  assert.deepEqual(LATE, [STEPS[1]]);
  assert.equal(SEEN_SCRIPT[SEEN_SCRIPT.length - 1], PCD.TERMINATE);
  assert.deepEqual(judgeMachine({ ranges: "ranges" }, { role: "server" }), []);
  assert.match(judgeMachine({ ranges: "ranges" }, { role: "player" }).join(), /nothing for seen/);
  assert.ok(buildNetRangesPk3().toString("latin1").includes("maps/MAP01.wad"));
  const member = (role, read) => ({ role, c: { rpc: async (_, args) => read(args) } });
  const passed = async () => ({ line: "PASS x: fine", failed: 0 });
  const set = (client) => ({ server: member("server", fakeTables()), clients: [client] });
  assert.equal((await judgeNetplay(set(member("player", fakeTables())), "MAP01 lan", {}, { judge: passed })).failed, 0);
  const r = await judgeNetplay(set(member("latejoin", fakeTables({ broken: true }))), "MAP01 lan", {}, { judge: passed });
  assert.match(r.line, /the late joiner: ACS translation 1 has 1 entries/);
  assert.match(r.line, /the late joiner: ACS translation 1 differs from the server's/);
  const logged = await judgeNetplay(set(member("player", fakeTables())), "MAP01 lan", {}, { judge: async () => ({ line: "FAIL MAP01 lan: the server: it logged nothing", failed: 1 }) });
  assert.match(logged.line, /^FAIL MAP01 lan: the server: it logged nothing$/);
});
