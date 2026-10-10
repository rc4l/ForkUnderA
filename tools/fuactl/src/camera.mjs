// [rc4l] GL's camera as fuactl reasons about it: heights stretched by the pixel aspect before the pitch, and a vertical field of view from 90 degrees over a 4:3 fovratio (ComputeBackendMVP is its C++ twin).
const DEG = Math.PI / 180;

// [rc4l] The pitch GL turns by, the playsim's seen through stretched pixels (up is negative, as viewpitch has it).
function stretchedPitch(pitchDeg, stretch) {
  const up = -pitchDeg * DEG;
  return Math.asin(Math.sin(up) * stretch / Math.hypot(Math.cos(up), Math.sin(up) * stretch));
}

// [rc4l] The facing (angle, pitch in degrees) that puts the crosshair on pixel (px, py) of a width x height capture taken from camera, so a probe can be aimed at what a screenshot shows.
export function pixelToFace(camera, px, py, width, height, { stretch = 1.2, fovY = 2 * Math.atan(0.75) } = {}) {
  const yaw = camera.angle * DEG, pc = stretchedPitch(camera.pitch || 0, stretch);
  const p5 = 1 / Math.tan(fovY / 2), p0 = p5 / (width / height);
  const nx = (px + 0.5 - width / 2) / (width / 2), ny = (height / 2 - (py + 0.5)) / (height / 2);
  const xe = nx / p0, ye = ny / p5;
  const fwd = Math.cos(pc) - ye * Math.sin(pc), up = (Math.sin(pc) + ye * Math.cos(pc)) / stretch;
  const dx = Math.cos(yaw) * fwd + Math.sin(yaw) * xe, dy = Math.sin(yaw) * fwd - Math.cos(yaw) * xe;
  return { angle: Math.atan2(dy, dx) / DEG, pitch: -Math.atan2(up, Math.hypot(dx, dy)) / DEG };
}

// [rc4l] Where a map point (x, y, height) lands in a width x height capture from camera (eye at camera.x, y, viewz), or null behind the eye.
export function pointToPixel(camera, point, width, height, { stretch = 1.2, fovY = 2 * Math.atan(0.75) } = {}) {
  const yaw = camera.angle * DEG, pc = stretchedPitch(camera.pitch || 0, stretch);
  const p5 = 1 / Math.tan(fovY / 2), p0 = p5 / (width / height);
  const rx = point.x - camera.x, ry = point.y - camera.y, rz = (point.z - camera.viewz) * stretch;
  const fh = Math.cos(yaw) * rx + Math.sin(yaw) * ry, side = Math.sin(yaw) * rx - Math.cos(yaw) * ry;
  const depth = Math.cos(pc) * fh + Math.sin(pc) * rz, ye = -Math.sin(pc) * fh + Math.cos(pc) * rz;
  if (depth <= 0) return null;
  return { x: width / 2 + (side / depth) * p0 * (width / 2) - 0.5, y: height / 2 - (ye / depth) * p5 * (height / 2) - 0.5 };
}
