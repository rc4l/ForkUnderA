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

function diff(a, b, tol) {
  if (a.w !== b.w || a.h !== b.h) return null;
  let bad = 0, sum = 0, n = 0, worstRow = -1, worstRowBad = 0;
  for (let y = 0; y < a.h; y++) {
    let rowBad = 0;
    for (let x = 0; x < a.w; x++) {
      const pa = px(a, x, y), pb = px(b, x, y);
      const d = Math.abs(pa[0] - pb[0]) + Math.abs(pa[1] - pb[1]) + Math.abs(pa[2] - pb[2]);
      sum += d; n++;
      if (d > tol) { bad++; rowBad++; }
    }
    if (rowBad > worstRowBad) { worstRowBad = rowBad; worstRow = y; }
  }
  return { pct: (100 * bad) / n, mean: sum / n / 3, worstRow, worstRowPct: (100 * worstRowBad) / a.w };
}

const [, , ...args] = process.argv;
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
