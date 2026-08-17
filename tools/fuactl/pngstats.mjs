// [rc4l] Mean RGB of a PNG region, so GL-vs-Vulkan comparisons are numbers instead of impressions.
//
// Three rounds of "the Vulkan render looks brighter" produced three different theories and no
// progress. A mean colour over the same patch of floor in both renderers answers in one line whether
// the difference is brightness (same hue, different magnitude) or palette (different hue).
//
// Minimal PNG decoder: enough for the engine's own RGB screenshots and readbacks.
import fs from "node:fs";
import zlib from "node:zlib";

function decodePNG(path) {
  const buf = fs.readFileSync(path);
  let p = 8, w = 0, h = 0, bitDepth = 0, colorType = 0;
  const idat = [];
  let palette = null;
  while (p < buf.length) {
    const len = buf.readUInt32BE(p);
    const type = buf.toString("ascii", p + 4, p + 8);
    const data = buf.subarray(p + 8, p + 8 + len);
    if (type === "IHDR") {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9];
    } else if (type === "PLTE") palette = data;
    else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    p += 12 + len;
  }
  if (bitDepth !== 8) throw new Error(`unsupported bit depth ${bitDepth}`);
  const channels = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colorType];
  if (!channels) throw new Error(`unsupported color type ${colorType}`);

  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = w * channels;
  const out = Buffer.alloc(h * stride);
  let prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, y * (stride + 1) + 1 + stride);
    const cur = Buffer.alloc(stride);
    for (let i = 0; i < stride; i++) {
      const a = i >= channels ? cur[i - channels] : 0;
      const b = prev[i];
      const c = i >= channels ? prev[i - channels] : 0;
      let v = line[i];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      cur[i] = v & 0xff;
    }
    cur.copy(out, y * stride);
    prev = cur;
  }
  return { w, h, channels, palette, colorType, data: out };
}

// Region given in FRACTIONS of the image, so two different resolutions can be compared.
function meanRegion(img, x0, y0, x1, y1) {
  const { w, h, channels, palette, colorType, data } = img;
  const ax = Math.floor(x0 * w), ay = Math.floor(y0 * h);
  const bx = Math.floor(x1 * w), by = Math.floor(y1 * h);
  let r = 0, g = 0, b = 0, n = 0;
  for (let y = ay; y < by; y++) {
    for (let x = ax; x < bx; x++) {
      const i = y * w * channels + x * channels;
      let pr, pg, pb;
      if (colorType === 3) { const q = data[i] * 3; pr = palette[q]; pg = palette[q + 1]; pb = palette[q + 2]; }
      else if (channels === 1 || channels === 2) { pr = pg = pb = data[i]; }
      else { pr = data[i]; pg = data[i + 1]; pb = data[i + 2]; }
      r += pr; g += pg; b += pb; n++;
    }
  }
  return n ? [r / n, g / n, b / n] : [0, 0, 0];
}

// [rc4l] Per-pixel agreement between two renders, because a mean cannot see shape.
//
// Mean colour was enough to catch brightness and palette drift and blind to everything else: a
// mirrored world barely moves the average of a roughly symmetric corridor, and a wall left standing
// across an open doorway moves it by a few percent. Both shipped as "matching" on the strength of a
// mean. What actually distinguishes "the same picture" from "a different picture" is how many pixels
// disagree and by how much.
// Resolve one pixel to RGB whatever the colour type is -- the engine writes truecolour, the readback
// writes truecolour, and an indexed PNG from anywhere else would otherwise diff as garbage.
function px(img, x, y) {
  const i = (y * img.w + x) * img.channels;
  if (img.colorType === 3) { const q = img.data[i] * 3; return [img.palette[q], img.palette[q + 1], img.palette[q + 2]]; }
  if (img.channels === 1 || img.channels === 2) { const v = img.data[i]; return [v, v, v]; }
  return [img.data[i], img.data[i + 1], img.data[i + 2]];
}

// [rc4l] CROP applies here too, as fractions "x0,y0,x1,y1". Asking "did the wall change" while the
// floor and the weapon sprite are in the same frame gets answered by whichever covers more pixels --
// which is how "GL is animating now" nearly got signed off on a floor that was already animating.
function diff(a, b, tol) {
  if (a.w !== b.w || a.h !== b.h) return null;
  const c = (process.env.CROP || "0,0,1,1").split(",").map(Number);
  const x0 = Math.floor(c[0] * a.w), y0 = Math.floor(c[1] * a.h);
  const x1 = Math.ceil(c[2] * a.w),  y1 = Math.ceil(c[3] * a.h);
  let bad = 0, sum = 0, n = 0, worstRow = -1, worstRowBad = 0;
  for (let y = y0; y < y1; y++) {
    let rowBad = 0;
    for (let x = x0; x < x1; x++) {
      const pa = px(a, x, y), pb = px(b, x, y);
      const d = Math.abs(pa[0] - pb[0]) + Math.abs(pa[1] - pb[1]) + Math.abs(pa[2] - pb[2]);
      sum += d; n++;
      if (d > tol) { bad++; rowBad++; }
    }
    if (rowBad > worstRowBad) { worstRowBad = rowBad; worstRow = y; }
  }
  return { pct: (100 * bad) / n, mean: sum / n / 3, worstRow,
           worstRowPct: (100 * worstRowBad) / Math.max(1, x1 - x0) };
}

// [rc4l] Write a PNG. Only what an 8-bit RGB image needs, which is all this ever emits.
function writePNG(path, w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 3 + 1)] = 0;                                  // filter: none
    rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, "ascii"), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  fs.writeFileSync(path, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0)),
  ]));
}

let CRC_TABLE = null;
function crc32(buf) {
  if (!CRC_TABLE) {
    CRC_TABLE = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      CRC_TABLE[n] = c;
    }
  }
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return c ^ 0xffffffff;
}

const [, , ...args] = process.argv;

// [rc4l] Where two renders disagree, as a picture.
//
// A single number says the renderers differ and cannot say whether it is the sky, the floor, one
// sprite or every wall edge -- and picking the next thing to port off a feature list is guessing.
// The heatmap answers it directly: the shape of the bright region names the feature.
//
// Output is the GL render dimmed to a quarter, with disagreement added in red. Keeping the scene
// visible underneath is the point; a bare difference mask shows bright pixels with nothing to say
// what they are sitting on.
if (args[0] === "--diffimg") {
  const [aPath, bPath, outPath] = args.slice(1);
  const a = decodePNG(aPath), b = decodePNG(bPath);
  if (a.w !== b.w || a.h !== b.h) { console.error("size mismatch"); process.exit(1); }
  const out = Buffer.alloc(a.w * a.h * 3);
  for (let y = 0; y < a.h; y++) {
    for (let x = 0; x < a.w; x++) {
      const pa = px(a, x, y), pb = px(b, x, y);
      const d = (Math.abs(pa[0] - pb[0]) + Math.abs(pa[1] - pb[1]) + Math.abs(pa[2] - pb[2])) / 3;
      const i = (y * a.w + x) * 3;
      out[i]     = Math.min(255, pa[0] * 0.25 + d * 3);
      out[i + 1] = Math.min(255, pa[1] * 0.25);
      out[i + 2] = Math.min(255, pa[2] * 0.25);
    }
  }
  writePNG(outPath, a.w, a.h, out);
  console.log(`wrote ${outPath}`);
  process.exit(0);
}

// [rc4l] Is the disagreement a misalignment rather than a difference?
//
// Once both renderers point-sample the same textures, a sub-texel offset stops being a soft blur and
// starts flipping whole texels, so a tiny raster or projection shift reads as a large per-pixel
// difference spread over the whole frame -- indistinguishable, by number alone, from a real shading
// bug. Scoring a few whole-pixel offsets tells the two apart: if the best score is at (0,0) the
// images are aligned and the difference is real, and if it is anywhere else the geometry is landing
// in the wrong place.
if (args[0] === "--align") {
  const a = decodePNG(args[1]), b = decodePNG(args[2]);
  const R = Number(process.env.RANGE || 2);
  // CROP restricts the search to one band of the frame, so a region with its own alignment -- the
  // sky, which is drawn by a different pipeline from everything else -- can be measured without the
  // walls and floor outvoting it.
  const crop = (process.env.CROP || "0,0,1,1").split(",").map(Number);
  const cx0 = Math.floor(crop[0] * a.w), cy0 = Math.floor(crop[1] * a.h);
  const cx1 = Math.ceil(crop[2] * a.w),  cy1 = Math.ceil(crop[3] * a.h);
  let best = null;
  for (let dy = -R; dy <= R; dy++) {
    for (let dx = -R; dx <= R; dx++) {
      let sum = 0, n = 0;
      for (let y = Math.max(cy0, -dy); y < Math.min(cy1, a.h - dy); y += 2) {
        for (let x = Math.max(cx0, -dx); x < Math.min(cx1, a.w - dx); x += 2) {
          const pa = px(a, x, y), pb = px(b, x + dx, y + dy);
          sum += Math.abs(pa[0] - pb[0]) + Math.abs(pa[1] - pb[1]) + Math.abs(pa[2] - pb[2]);
          n++;
        }
      }
      const m = sum / n / 3;
      if (!best || m < best.m) best = { dx, dy, m };
    }
  }
  const name = args[1].split(/[\/]/).pop();
  console.log(`${name.padEnd(24)} best offset dx=${best.dx} dy=${best.dy}  mean|d| ${best.m.toFixed(1)}`);
  process.exit(0);
}

// [rc4l] Cut a region out and magnify it, because an artefact three pixels wide is invisible in a
// 640x480 frame and obvious at 4x. Fractions, like every other region argument here.
//   node pngstats.mjs --crop in.png out.png x0 y0 x1 y1 [zoom]
// [rc4l] Where the darkest thing in a region is, as a fraction of the frame.
//
// A decal on a dim floor is a dark blob and nothing else in the crop is, so its centroid is a
// number rather than an impression -- which is what "does it stay put when the camera moves" needs.
// Two frames from different camera angles should move the blob by the same amount they move
// everything else; a blob that does not move is stuck to the screen.
//   node pngstats.mjs --blob in.png [x0 y0 x1 y1]
// [rc4l] Where two frames differ most, as a fraction of the frame.
//
// --blob finds the darkest thing, which on a near-black floor is not reliably the decal at all.
// Diffing against the same camera BEFORE the decal existed isolates it exactly: whatever changed is
// the decal and nothing else. That is what makes "does it stay anchored when the camera moves" a
// measurement rather than a squint.
//   node pngstats.mjs --diffblob before.png after.png [x0 y0 x1 y1]
if (args[0] === "--diffblob") {
  const a = decodePNG(args[1]), b = decodePNG(args[2]);
  if (a.w !== b.w || a.h !== b.h) { console.log("SIZE MISMATCH"); process.exit(0); }
  const f = args.length >= 7 ? args.slice(3, 7).map(Number) : [0, 0, 1, 1];
  const x0 = Math.floor(f[0] * a.w), y0 = Math.floor(f[1] * a.h);
  const x1 = Math.ceil(f[2] * a.w),  y1 = Math.ceil(f[3] * a.h);
  const diffs = [];
  for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
    const pa = px(a, x, y), pb = px(b, x, y);
    const d = Math.abs(pa[0]-pb[0]) + Math.abs(pa[1]-pb[1]) + Math.abs(pa[2]-pb[2]);
    if (d > 8) diffs.push([d, x, y]);
  }
  if (diffs.length === 0) { console.log("no difference"); process.exit(0); }
  diffs.sort((p, q) => q[0] - p[0]);
  const take = Math.max(1, Math.floor(diffs.length * 0.5));
  let sx = 0, sy = 0;
  for (let i = 0; i < take; i++) { sx += diffs[i][1]; sy += diffs[i][2]; }
  console.log(`changed at x ${(sx / take / a.w).toFixed(4)} y ${(sy / take / a.h).toFixed(4)} ` +
              `(${diffs.length} px changed)`);
  process.exit(0);
}
if (args[0] === "--blob") {
  const a = decodePNG(args[1]);
  const f = args.length >= 6 ? args.slice(2, 6).map(Number) : [0, 0, 1, 1];
  const x0 = Math.floor(f[0] * a.w), y0 = Math.floor(f[1] * a.h);
  const x1 = Math.ceil(f[2] * a.w),  y1 = Math.ceil(f[3] * a.h);
  const lum = [];
  for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
    const p0 = px(a, x, y);
    lum.push([0.299 * p0[0] + 0.587 * p0[1] + 0.114 * p0[2], x, y]);
  }
  lum.sort((p, q) => p[0] - q[0]);
  const take = Math.max(1, Math.floor(lum.length * 0.02));   // darkest 2%
  let sx = 0, sy = 0;
  for (let i = 0; i < take; i++) { sx += lum[i][1]; sy += lum[i][2]; }
  console.log(`blob at x ${(sx / take / a.w).toFixed(4)} y ${(sy / take / a.h).toFixed(4)} ` +
              `(darkest ${take} px, mean lum ${(lum[0][0]).toFixed(1)})`);
  process.exit(0);
}
// [rc4l] GAIN brightens the crop. Doom rooms are frequently near-black, and a decal a metre wide can
// sit at rgb 12,10,10 on a floor at 8,7,7 -- present, correct, and invisible in a screenshot. Turning
// the light amplifier on instead is not a substitute: it is a GL colormap the backend does not
// implement, so it lights one window and not the other, which is worse than dark.
if (args[0] === "--crop") {
  const a = decodePNG(args[1]);
  const f = args.slice(3, 7).map(Number);
  const z = Number(args[7] || 4);
  const gain = Number(process.env.GAIN || 1);
  const x0 = Math.floor(f[0] * a.w), y0 = Math.floor(f[1] * a.h);
  const x1 = Math.ceil(f[2] * a.w),  y1 = Math.ceil(f[3] * a.h);
  const w = (x1 - x0) * z, h = (y1 - y0) * z;
  const out = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const p0 = px(a, x0 + Math.floor(x / z), y0 + Math.floor(y / z));
      const o = (y * w + x) * 3;
      for (let c = 0; c < 3; c++) out[o + c] = Math.min(255, Math.round(p0[c] * gain));
    }
  writePNG(args[2], w, h, out);
  console.log(`wrote ${args[2]} ${w}x${h} (${z}x of ${x1 - x0}x${y1 - y0}${gain !== 1 ? `, gain ${gain}` : ""})`);
  process.exit(0);
}
// [rc4l] Per-row luminance, and the rows where it STEPS.
//
// A seam across a sprite is a horizontal discontinuity, and the eye can see one but not say where it
// is -- which is the only thing that identifies what drew it. Reading the profile and its biggest
// jumps turns "somewhere below the middle" into a row number and then into a world height.
//   node pngstats.mjs --rows in.png [x0 x1] [topN]
// [rc4l] The difference between two frames, as a picture.
//
// --diff gives a percentage and --diffblob a centroid, and neither answers "what SHAPE is the
// difference" -- which is the question whenever one renderer has an artifact the other does not. A
// seam, a band, a halo and a wholesale shift all score about the same and look nothing alike.
//   node pngstats.mjs --diffimg a.png b.png out.png [gain] [x0 y0 x1 y1] [zoom]
if (args[0] === "--diffimg") {
  const a = decodePNG(args[1]), b = decodePNG(args[2]);
  if (a.w !== b.w || a.h !== b.h) { console.log("SIZE MISMATCH"); process.exit(2); }
  const gain = Number(args[4] || 4);
  const f = args.length >= 9 ? args.slice(5, 9).map(Number) : [0, 0, 1, 1];
  const z = Number(args[9] || 1);
  const x0 = Math.floor(f[0] * a.w), y0 = Math.floor(f[1] * a.h);
  const x1 = Math.ceil(f[2] * a.w),  y1 = Math.ceil(f[3] * a.h);
  const w = (x1 - x0) * z, h = (y1 - y0) * z;
  const out = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const sx = x0 + Math.floor(x / z), sy = y0 + Math.floor(y / z);
      const pa = px(a, sx, sy), pb = px(b, sx, sy);
      const o = (y * w + x) * 3;
      for (let c = 0; c < 3; c++) out[o + c] = Math.min(255, Math.abs(pa[c] - pb[c]) * gain);
    }
  writePNG(args[3], w, h, out);
  console.log(`wrote ${args[3]} ${w}x${h} (gain ${gain})`);
  process.exit(0);
}
if (args[0] === "--rows") {
  const a = decodePNG(args[1]);
  const fx0 = args.length >= 4 ? Number(args[2]) : 0.35;
  const fx1 = args.length >= 4 ? Number(args[3]) : 0.65;
  const topN = Number(args[4] || 6);
  const x0 = Math.floor(fx0 * a.w), x1 = Math.ceil(fx1 * a.w);
  const lum = [];
  for (let y = 0; y < a.h; y++) {
    let s = 0;
    for (let x = x0; x < x1; x++) { const p = px(a, x, y); s += (p[0] + p[1] + p[2]) / 3; }
    lum.push(s / (x1 - x0));
  }
  const steps = [];
  for (let y = 1; y < a.h; y++) steps.push([Math.abs(lum[y] - lum[y - 1]), y]);
  steps.sort((p, q) => q[0] - p[0]);
  console.log(`${args[1].split(/[\/]/).pop()}  rows ${a.h}, x ${x0}..${x1}`);
  for (let i = 0; i < topN; i++) {
    const [d, y] = steps[i];
    console.log(`  step ${d.toFixed(1).padStart(6)} at y=${String(y).padStart(3)} ` +
                `(${(y / a.h * 100).toFixed(1)}%)  ${lum[y - 1].toFixed(1)} -> ${lum[y].toFixed(1)}`);
  }
  process.exit(0);
}
if (args[0] === "--diff") {
  const tol = Number(process.env.TOL || 24);
  const pairs = args.slice(1);
  for (let i = 0; i + 1 < pairs.length; i += 2) {
    const a = decodePNG(pairs[i]), b = decodePNG(pairs[i + 1]);
    const d = diff(a, b, tol);
    const name = pairs[i].split(/[\/]/).pop();
    if (!d) { console.log(`${name.padEnd(24)} SIZE MISMATCH ${a.w}x${a.h} vs ${b.w}x${b.h}`); continue; }
    console.log(`${name.padEnd(24)} differ ${d.pct.toFixed(1).padStart(5)}%  mean|d| ${d.mean.toFixed(1).padStart(5)}` +
      `  worst row y=${String(d.worstRow).padStart(3)} (${d.worstRowPct.toFixed(0)}%)`);
  }
  process.exit(0);
}
if (args.length < 1) {
  console.error("usage: node pngstats.mjs <a.png> [b.png] [x0 y0 x1 y1]  (region as 0..1 fractions)");
  process.exit(2);
}
// [rc4l] The region is present when the last four arguments are NUMBERS, not when there happen to be
// six of them. The count test needed two files to work and silently treated the region of a
// single-file call as four more filenames -- which then reported the whole frame and crashed on
// "0.33", so a measurement that looked like it had a region never had one.
const tail = args.slice(-4).map(Number);
const hasRegion = args.length >= 5 && tail.every((v) => Number.isFinite(v));
const region = hasRegion ? tail : [0.3, 0.55, 0.7, 0.8];
const files = hasRegion ? args.slice(0, -4) : args;
for (const f of files) {
  const img = decodePNG(f);
  const [r, g, b] = meanRegion(img, ...region);
  const lum = 0.299 * r + 0.587 * g + 0.114 * b;
  console.log(
    `${f.padEnd(46)} ${img.w}x${img.h}  mean rgb ${r.toFixed(1)},${g.toFixed(1)},${b.toFixed(1)}` +
    `  lum ${lum.toFixed(1)}  hue r/b ${(r / Math.max(b, 0.01)).toFixed(2)}`);
}
