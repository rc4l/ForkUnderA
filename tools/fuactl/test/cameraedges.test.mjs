// [Doruk-Aksoy] cameraedges' map and its three-screen judge, on synthetic pictures.
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildCameraEdgesWad, classify, boxPixels, judgeScreens, SCREEN_BOXES, WANT, CAMERAS, DECAL_ID, DECAL_THING, YELLOW_FOUNTAIN, ZOMBIEMAN, LINE_MIRROR } from "../scenarios/cameraedges.mjs";
import { readWad, parseUdmf } from "../src/udmf.mjs";

const W = 400, H = 200;
const SHADE = { green: [20, 200, 20], yellow: [140, 100, 0], magenta: [200, 30, 200] };
// [Doruk-Aksoy] A dark picture with, inside each named screen's box, the given count of pixels of each colour.
function picture(fill) {
  const data = Buffer.alloc(W * H * 3, 20);
  for (const [screen, colours] of Object.entries(fill)) {
    const [l, t, r] = SCREEN_BOXES[screen];
    const x0 = Math.ceil(W * l), y0 = Math.ceil(H * t), across = Math.floor(W * r) - x0;
    let i = 0;
    for (const [colour, n] of Object.entries(colours))
      for (let k = 0; k < n; k++, i++) data.set(SHADE[colour], ((y0 + Math.floor(i / across)) * W + x0 + (i % across)) * 3);
  }
  return { w: W, h: H, channels: 3, data };
}
const passing = () => ({ A: { ...WANT.A }, B: { ...WANT.B }, C: { ...WANT.C } });

test("cameraedges' map holds three cameras and screens, a decal of its own number, the fountain, a dormant zombieman and a mirror", () => {
  const wad = readWad(buildCameraEdgesWad());
  const map = parseUdmf(wad.find((l) => l.name === "TEXTMAP").data.toString("latin1"));
  const things = map.things || map.thing, lines = map.linedefs || map.linedef;
  assert.deepEqual(things.filter((t) => t.type === 9001).map((t) => t.id).sort(), Object.values(CAMERAS).sort());
  const decal = things.find((t) => t.type === DECAL_THING);
  assert.equal(decal.arg0 + (decal.arg1 << 8), DECAL_ID);
  assert.ok(things.some((t) => t.type === YELLOW_FOUNTAIN));
  assert.ok(things.find((t) => t.type === ZOMBIEMAN).dormant);
  assert.equal(lines.filter((l) => l.special === LINE_MIRROR).length, 1);
  assert.match(wad.find((l) => l.name === "DECALDEF").data.toString(), new RegExp(`FuaCameraMark ${DECAL_ID}`));
});

test("classify tells the columns' green, the particles' yellow and the decal's magenta from the rest", () => {
  assert.equal(classify(...SHADE.green), "green");
  assert.equal(classify(...SHADE.yellow), "yellow");
  assert.equal(classify(...SHADE.magenta), "magenta");
  assert.equal(classify(20, 20, 20), null);
  assert.equal(classify(0, 0, 170), null);
});

test("boxPixels counts each colour inside one screen's box only", () => {
  const p = picture({ A: { green: 7, magenta: 3 }, B: { yellow: 5 } });
  assert.deepEqual(boxPixels(p, SCREEN_BOXES.A), { green: 7, yellow: 0, magenta: 3 });
  assert.deepEqual(boxPixels(p, SCREEN_BOXES.B), { green: 0, yellow: 5, magenta: 0 });
});

test("judgeScreens passes each screen showing its own room, fails one missing it, and fails another room's colour leaking onto it", () => {
  assert.equal(judgeScreens(picture(passing())).failed, 0);
  const bare = judgeScreens(picture({}));
  assert.equal(bare.failed, 4);
  assert.ok(bare.lines.some((l) => /^FAIL screen C shows its room's green: 0 pixels/.test(l)));
  const leak = passing();
  leak.B.green = 30;
  const leaked = judgeScreens(picture(leak));
  assert.equal(leaked.failed, 1);
  assert.match(leaked.lines.find((l) => l.startsWith("FAIL")), /^FAIL screen B shows no other room's green: 30 pixels/);
});
