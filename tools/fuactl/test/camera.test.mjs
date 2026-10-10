import { test } from "node:test";
import assert from "node:assert/strict";
import { pixelToFace, stretchedPitch } from "../src/camera.mjs";

const DEG = Math.PI / 180;

// [rc4l] The forward projection pixelToFace inverts: where a map point (x, y, height) lands in a capture from camera, or null behind the eye.
function pointToPixel(camera, point, width, height, stretch = 1.2, fovY = 2 * Math.atan(0.75)) {
  const yaw = camera.angle * DEG, pc = stretchedPitch(camera.pitch || 0, stretch);
  const p5 = 1 / Math.tan(fovY / 2), p0 = p5 / (width / height);
  const rx = point.x - camera.x, ry = point.y - camera.y, rz = (point.z - camera.viewz) * stretch;
  const fh = Math.cos(yaw) * rx + Math.sin(yaw) * ry, side = Math.sin(yaw) * rx - Math.cos(yaw) * ry;
  const depth = Math.cos(pc) * fh + Math.sin(pc) * rz, ye = -Math.sin(pc) * fh + Math.cos(pc) * rz;
  if (depth <= 0) return null;
  return { x: width / 2 + (side / depth) * p0 * (width / 2) - 0.5, y: height / 2 - (ye / depth) * p5 * (height / 2) - 0.5 };
}

// [rc4l] The stackportal template's upper room, where GL put the far corner's top on row 345 of 640 looking up 40 degrees.
const CORNER = { x: 448, y: 448, viewz: 169, angle: 225, pitch: -40 };

test("a point lands where GL drew it", () => {
  const p = pointToPixel(CORNER, { x: 0, y: 0, z: 640 }, 1024, 640);
  assert.ok(Math.abs(p.x - 511.5) < 0.5, `x ${p.x}`);
  assert.ok(Math.abs(p.y - 345) < 1.5, `y ${p.y}`);
  assert.equal(pointToPixel(CORNER, { x: 900, y: 900, z: 169 }, 1024, 640), null);   // [rc4l] behind the eye
});

test("the middle of the frame is the camera's own facing", () => {
  const f = pixelToFace(CORNER, 511.5, 319.5, 1024, 640);
  assert.ok(Math.abs(f.angle - 225 + 360) < 1e-6 || Math.abs(f.angle - 225) < 1e-6, `angle ${f.angle}`);
  assert.ok(Math.abs(f.pitch + 40) < 1e-6, `pitch ${f.pitch}`);
});

test("aiming at a pixel and projecting back along that aim returns the pixel", () => {
  for (const [px, py] of [[100, 50], [900, 600], [512, 345], [3, 320]]) {
    const f = pixelToFace(CORNER, px, py, 1024, 640);
    const d = 500, r = f.angle * Math.PI / 180, up = -f.pitch * Math.PI / 180;
    const point = { x: CORNER.x + d * Math.cos(r), y: CORNER.y + d * Math.sin(r), z: CORNER.viewz + d * Math.tan(up) };
    const back = pointToPixel(CORNER, point, 1024, 640);
    assert.ok(Math.abs(back.x - px) < 1e-6 && Math.abs(back.y - py) < 1e-6, `${px},${py} -> ${back.x},${back.y}`);
  }
});

test("with no stretch the camera is a square-pixel one", () => {
  const f = pixelToFace({ angle: 0, pitch: 0 }, 511.5, 0, 1024, 640, { stretch: 1 });
  assert.ok(Math.abs(f.angle) < 1e-6);
  assert.ok(f.pitch < -30 && f.pitch > -40, `pitch ${f.pitch}`);   // [rc4l] the top row, a little under half the 73.7 degree field
});
