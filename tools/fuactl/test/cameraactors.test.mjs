// [Doruk-Aksoy] cameraactors' map and its screen judge, on synthetic pictures.
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildCameraActorsWad, screenPixels, judgeScreen, COLUMNS, GREEN_PIXELS, YELLOW_PIXELS, RED_PIXELS, SCREEN_BOX, YELLOW_FOUNTAIN } from "../scenarios/cameraactors.mjs";
import { readWad, parseUdmf } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";

const W = 200, H = 200;
// [Doruk-Aksoy] A dark picture with this many red, then green, then particle-yellow pixels inside the screen's box, and a green row above it, as the side walls' panels are.
const SHADES = [[200, 20, 20], [20, 200, 20], [140, 100, 0]];
function picture(red, green, yellow = 0) {
  const data = Buffer.alloc(W * H * 3, 20);
  const x0 = Math.ceil(W * SCREEN_BOX[0]), y0 = Math.ceil(H * SCREEN_BOX[1]), across = Math.floor(W * SCREEN_BOX[2]) - x0;
  for (let i = 0; i < red + green + yellow; i++) data.set(SHADES[i < red ? 0 : i < red + green ? 1 : 2], ((y0 + Math.floor(i / across)) * W + x0 + (i % across)) * 3);
  for (let x = 0; x < W; x++) data[x * 3 + 1] = 200;
  return { w: W, h: H, channels: 3, data };
}

test("cameraactors' map holds the columns in the camera's room, apart from the player's", () => {
  const wad = readWad(buildCameraActorsWad());
  const map = parseUdmf(wad.find((l) => l.name === "TEXTMAP").data.toString("latin1"));
  const things = map.things || map.thing;
  const columns = things.filter((t) => t.type === THING.TallGreenColumn);
  assert.equal(columns.length, COLUMNS.length);
  assert.equal(things.filter((t) => t.type === YELLOW_FOUNTAIN).length, 1);
  const camera = things.find((t) => t.type === 9001);
  const player = things.find((t) => t.type === 1);
  assert.ok(columns.every((t) => t.x > camera.x && t.x > 1792));
  assert.ok(player.x < 512 && camera.x > 1792);
  assert.ok(wad.some((l) => l.name === "ANIMDEFS") && wad.some((l) => l.name === "BEHAVIOR"));
});

test("screenPixels counts green, yellow and red pixels inside the box apart from the rest", () => {
  assert.deepEqual(screenPixels(picture(7, 5, 3)), { green: 5, yellow: 3, red: 7 });
});

test("judgeScreen passes the columns and particles on the screen, fails the room without either, and fails a screen without the room", () => {
  assert.equal(judgeScreen(picture(RED_PIXELS, GREEN_PIXELS, YELLOW_PIXELS)).failed, 0);
  const bare = judgeScreen(picture(RED_PIXELS, 0, 0));
  assert.equal(bare.failed, 2);
  assert.match(bare.lines[0], /^FAIL the camera's columns on the screen: 0 green pixels/);
  assert.match(bare.lines[1], /^FAIL the camera's particles on the screen: 0 yellow pixels/);
  assert.equal(judgeScreen(picture(RED_PIXELS, GREEN_PIXELS, 0)).failed, 1);
  assert.match(judgeScreen(picture(RED_PIXELS - 1, GREEN_PIXELS, YELLOW_PIXELS)).lines[0], /^FAIL the screen shows \d+ red floor pixels/);
});
