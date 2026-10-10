// [rc4l] PNG reading, writing and comparison shared by `fuactl png`, the scenarios and their tests.
import fs from "node:fs";
import zlib from "node:zlib";

export function decodePNG(path) {
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

// [rc4l] One pixel as RGB, whatever the colour type.
export function px(img, x, y) {
  const i = (y * img.w + x) * img.channels;
  if (img.colorType === 3) { const q = img.data[i] * 3; return [img.palette[q], img.palette[q + 1], img.palette[q + 2]]; }
  if (img.channels === 1 || img.channels === 2) { const v = img.data[i]; return [v, v, v]; }
  return [img.data[i], img.data[i + 1], img.data[i + 2]];
}

// [rc4l] The RGB at each [x, y] point, for reading a handful of pixels off several captures at once.
export function samplePoints(img, points) {
  return points.map(([x, y]) => px(img, x, y));
}

// [rc4l] Whether a capture holds nothing, every channel at most threshold: what a GL readback returns when another window covers it.
export function isBlankImage(img, threshold = 2) {
  for (let y = 0; y < img.h; y += 4)
    for (let x = 0; x < img.w; x += 4)
      if (px(img, x, y).some((c) => c > threshold)) return false;
  return true;
}

// [rc4l] Write an 8-bit RGB PNG, which is all this ever emits.
export function writePNG(path, w, h, rgb) {
  fs.writeFileSync(path, encodePNG(w, h, rgb));
}

// [rc4l] A box of an image (fractions x0,y0,x1,y1), scaled by zoom (below 1 shrinks it, to whole pixels) and brightened by gain.
export function cropImage(img, box, zoom = 4, gain = 1) {
  const x0 = Math.floor(box[0] * img.w), y0 = Math.floor(box[1] * img.h);
  const x1 = Math.ceil(box[2] * img.w),  y1 = Math.ceil(box[3] * img.h);
  const w = Math.max(1, Math.round((x1 - x0) * zoom)), h = Math.max(1, Math.round((y1 - y0) * zoom));
  const rgb = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const p = px(img, Math.min(x1 - 1, x0 + Math.floor(x / zoom)), Math.min(y1 - 1, y0 + Math.floor(y / zoom)));
      for (let c = 0; c < 3; c++) rgb[(y * w + x) * 3 + c] = Math.min(255, Math.round(p[c] * gain));
    }
  return { w, h, rgb, srcW: x1 - x0, srcH: y1 - y0 };
}

// [rc4l] An RGB image as PNG bytes, for a capture on disk or a graphic lump inside a template wad.
export function encodePNG(w, h, rgb) {
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
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0)),
  ]);
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

// [rc4l] Mean |d| over a box (fractions x0,y0,x1,y1) in block means, so sub-pixel framing between renderers averages out (block 1 is per pixel).
export function boxDiff(a, b, box, block = 16) {
  // [rc4l] Pixel coordinates passed by mistake made a box hundreds of frames wide, and the walk over it never came back.
  if (box.some((v) => !(v >= 0 && v <= 1))) throw new Error(`box is fractions of the frame, 0..1: ${box.join(",")}`);
  const x0 = Math.floor(box[0] * a.w), y0 = Math.floor(box[1] * a.h);
  const x1 = Math.ceil(box[2] * a.w), y1 = Math.ceil(box[3] * a.h);
  const mean = (img, bx, by) => {
    const s = [0, 0, 0];
    let n = 0;
    for (let y = by; y < Math.min(by + block, y1); y++) for (let x = bx; x < Math.min(bx + block, x1); x++) {
      const q = px(img, x, y);
      s[0] += q[0]; s[1] += q[1]; s[2] += q[2]; n++;
    }
    return s.map((v) => v / Math.max(n, 1));
  };
  let sum = 0, n = 0;
  for (let by = y0; by < y1; by += block) for (let bx = x0; bx < x1; bx += block) {
    const ma = mean(a, bx, by), mb = mean(b, bx, by);
    sum += (Math.abs(ma[0] - mb[0]) + Math.abs(ma[1] - mb[1]) + Math.abs(ma[2] - mb[2])) / 3;
    n++;
  }
  return sum / Math.max(n, 1);
}

// [rc4l] A frozen A/B: what switching the value changed, against the repeat of the first value, which is the noise floor.
export function abDiff(a, b, a2, box = [0, 0, 1, 1], block = 16) {
  return { changed: boxDiff(a, b, box, block), noise: boxDiff(a, a2, box, block) };
}
