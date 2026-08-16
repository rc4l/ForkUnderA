// [rc4l] skyprobe -- measure what the sky tint actually does, per map, from a spot where the sky
// can be seen.
//
// Written after doing the same thing badly by hand a dozen times in one session. Three mistakes kept
// recurring, and each is designed out here rather than left to discipline:
//
//   1. Judging an outdoor effect from the player start. On a lot of maps that is indoors, and the
//      frames come back near-black. One such pair measured a dark room at R12 G9 B7 and was very
//      nearly reported as evidence about a sky. The engine now names an outdoor spot
//      (fua_skytintinfo prints `warp X Y`) and this warps there first.
//   2. Reading pixels without proving which map is loaded. The sector count from the same diagnostic
//      is checked into the output, so a frame can always be tied to the level it came from.
//   3. Comparing in RGB. A per-channel difference says little about what an eye sees; this reports
//      CIELAB dE76 and chroma alongside the raw means.
//
// Usage: fuactl skyprobe --port P [--token T] [--maps MAP01,MAP20] [--strength N] [--saturation N]
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { BridgeClient } from "./client.mjs";
import * as ui from "./ui.mjs";
import { makeUndisturbed, warpTo, loadMap } from "./undisturbed.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function linear(v) {
  const s = v / 255;
  return s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
}

function labF(t) {
  return t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116;
}

// sRGB -> XYZ (D65) -> CIELAB. Distances here track how different two colours look; distances in
// RGB do not, which is why every earlier hand-rolled comparison was hard to interpret.
function toLab([r, g, b]) {
  const lr = linear(r), lg = linear(g), lb = linear(b);
  const X = 0.4124 * lr + 0.3576 * lg + 0.1805 * lb;
  const Y = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;
  const Z = 0.0193 * lr + 0.1192 * lg + 0.9505 * lb;
  const fx = labF(X / 0.95047), fy = labF(Y / 1.0), fz = labF(Z / 1.08883);
  return [116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)];
}

// How much light a tint passes, Rec.709 on the multiplier itself. This is the number that tracked
// the "too strong" / "too weak" complaints when nothing else did: a tint that keeps the scene bright
// reads as vividly coloured, one that halves it reads as dim and muddy whatever its saturation.
function passThrough([r, g, b]) {
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
}

// Minimal PNG reader: enough for the screenshots the engine writes (8-bit RGB/RGBA, no interlace).
// Pulling in a decoder for one mean would be a dependency this tool does not need. Reads the 3D view
// only, skipping the HUD and console overlay, which otherwise drag every mean toward whatever colour
// the status bar happens to be.
function decodePngMean(file) {
  const buf = fs.readFileSync(file);
  let pos = 8, width = 0, height = 0, bitDepth = 0, colorType = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString("ascii", pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === "IHDR") {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9];
      if (bitDepth !== 8 || (colorType !== 2 && colorType !== 6) || data[12] !== 0) {
        throw new Error(`skyprobe: unsupported PNG (depth ${bitDepth} type ${colorType})`);
      }
    } else if (type === "IDAT") {
      idat.push(Buffer.from(data));
    } else if (type === "IEND") break;
    pos += 12 + len;
  }

  const raw = zlib.inflateSync(Buffer.concat(idat));
  const channels = colorType === 6 ? 4 : 3;
  const stride = width * channels;
  const out = Buffer.alloc(height * stride);
  let prev = Buffer.alloc(stride);

  // PNG filters, per scanline. Unavoidable: the bytes are meaningless without undoing them.
  for (let y = 0, at = 0; y < height; y++) {
    const filter = raw[at++];
    const line = raw.subarray(at, at + stride);
    at += stride;
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
        const p = a + b - c;
        const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[i] = v & 0xff;
    }
    cur.copy(out, y * stride);
    prev = cur;
  }

  let r = 0, g = 0, b = 0, n = 0;
  const top = 60, bottom = Math.min(height, 360);
  for (let y = top; y < bottom; y += 2) {
    for (let x = 0; x < width; x += 2) {
      const i = y * stride + x * channels;
      r += out[i]; g += out[i + 1]; b += out[i + 2]; n++;
    }
  }
  return n ? [r / n, g / n, b / n] : [0, 0, 0];
}

async function ask(c, text, waitMs = 900) {
  const lines = [];
  const off = c.onEvent((n, d) => { if (n === "out" && d && d.text) lines.push(d.text.trim()); });
  await c.rpc("console.exec", { text });
  await sleep(waitMs);
  off();
  return lines;
}

export async function runSkyProbe(opts) {
  const { port, token, engine, maps, strength, saturation, outDir } = opts;

  const c = new BridgeClient();
  await c.connect(Number(port), { token, timeoutMs: 8000 });
  await c.waitHello();
  await makeUndisturbed(c);

  for (const [k, v] of Object.entries({
    cl_fua_skytint: 1,
    cl_fua_skytint_strength: strength,
    cl_fua_skytint_saturation: saturation,
  })) {
    if (v != null) await c.rpc("console.exec", { text: `${k} ${v}` });
  }

  const rows = [];
  for (const map of maps) {
    await loadMap(c, map);

    const info = await ask(c, "fua_skytintinfo", 1200);
    const joined = info.filter((l) => l.startsWith("skytint:")).join(" ");

    const sectors = /sectors=(\d+)/.exec(joined)?.[1] ?? "?";
    const any = /any=(\d)/.exec(joined)?.[1] ?? "?";
    const tint = /tint\[(\d+),(\d+),(\d+)\]/.exec(joined);
    const solved = /solved=(-?\d+)%/.exec(joined)?.[1] ?? "-";
    const spot = /warp (-?\d+) (-?\d+)/.exec(joined);

    // Stand somewhere the sky is visible. Without this the frames below are whatever the player
    // start happens to look at, which is the single most common way this measurement goes wrong.
    const warped = spot ? await warpTo(c, Number(spot[1]), Number(spot[2])) : false;

    const shots = {};
    for (const [val, name] of [["1", "on"], ["0", "off"]]) {
      await c.rpc("console.exec", { text: `cl_fua_skytint ${val}` });
      await sleep(1400);
      const base = `probe-${map}-${name}`;
      const file = path.join(outDir, `${base}.png`);
      if (fs.existsSync(file)) fs.unlinkSync(file);
      await ui.screenshot(c, engine, base);
      shots[name] = file;
    }
    await c.rpc("console.exec", { text: "cl_fua_skytint 1" });

    let sceneMean = null, resultMean = null, dE = null, chroma = null;
    if (fs.existsSync(shots.off) && fs.existsSync(shots.on)) {
      sceneMean = decodePngMean(shots.off);
      resultMean = decodePngMean(shots.on);
      const a = toLab(sceneMean), b = toLab(resultMean);
      dE = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
      chroma = Math.hypot(b[1], b[2]);
    }

    rows.push({
      map, sectors, any, solved, warped,
      tint: tint ? [Number(tint[1]), Number(tint[2]), Number(tint[3])] : null,
      sceneMean, resultMean, dE, chroma,
    });
  }
  c.close();

  const pad = (s, n) => String(s).padEnd(n);
  console.log(pad("map", 8) + pad("sectors", 8) + pad("any", 4) + pad("tint", 16) +
    pad("passes", 8) + pad("solved", 8) + pad("outdoors", 9) + pad("dE76", 8) + "chroma");
  for (const r of rows) {
    const tintStr = r.tint ? `${r.tint[0]},${r.tint[1]},${r.tint[2]}` : "-";
    const pass = r.tint ? `${Math.round(passThrough(r.tint) * 100)}%` : "-";
    console.log(pad(r.map, 8) + pad(r.sectors, 8) + pad(r.any, 4) + pad(tintStr, 16) +
      pad(pass, 8) + pad(r.solved + "%", 8) + pad(r.warped ? "yes" : "NO", 9) +
      pad(r.dE == null ? "-" : r.dE.toFixed(2), 8) +
      (r.chroma == null ? "-" : r.chroma.toFixed(2)));
  }

  // Stated rather than left to be noticed. A row measured from the player start is not comparable
  // with one measured outdoors, and mixing them silently is how this went wrong before.
  const indoors = rows.filter((r) => !r.warped).map((r) => r.map);
  if (indoors.length) {
    console.log(`\nNOTE: no outdoor spot found for ${indoors.join(", ")} -- those rows were ` +
      `measured wherever the player spawned and are not comparable with the others.`);
  }

  return rows;
}
