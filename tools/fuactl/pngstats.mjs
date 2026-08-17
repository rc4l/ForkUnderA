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
const region = args.length >= 6 ? args.slice(-4).map(Number) : [0.3, 0.55, 0.7, 0.8];
const files = args.length >= 6 ? args.slice(0, -4) : args;
for (const f of files) {
  const img = decodePNG(f);
  const [r, g, b] = meanRegion(img, ...region);
  const lum = 0.299 * r + 0.587 * g + 0.114 * b;
  console.log(
    `${f.padEnd(46)} ${img.w}x${img.h}  mean rgb ${r.toFixed(1)},${g.toFixed(1)},${b.toFixed(1)}` +
    `  lum ${lum.toFixed(1)}  hue r/b ${(r / Math.max(b, 0.01)).toFixed(2)}`);
}
