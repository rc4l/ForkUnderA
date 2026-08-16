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

const [, , ...args] = process.argv;
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
