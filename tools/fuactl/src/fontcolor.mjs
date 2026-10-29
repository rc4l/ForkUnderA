// [Doruk-Aksoy] A truecolor font's colours as the engine works them out (features/truecolorfont), so a template can draw the glyphs it expects recoloured as plain images.
import { Buffer } from "node:buffer";

// [Doruk-Aksoy] A colour's brightness, 0 to 255.
export const luminance = (r, g, b) => (r * 77 + g * 143 + b * 37) >> 8;

const clamp255 = (v) => (v < 0 ? 0 : v > 255 ? 255 : v);

// [Doruk-Aksoy] The [r, g, b] brightness level v gets in a TEXTCOLO colour, its steps `{ start, end, from, to }` in rising order.
export function rangeColor(steps, v) {
  let i = 0;
  while (!(steps[i].start <= v && steps[i].end >= v) && i + 1 < steps.length) i++;
  const s = steps[i];
  const rangev = Math.trunc(((v - s.start) * 256) / (s.end - s.start));
  return s.from.map((f, c) => clamp255(((f << 8) + rangev * (s.to[c] - f)) >> 8));
}

// [Doruk-Aksoy] The colour's 256 brightness levels.
export const luminosityRamp = (steps) => Array.from({ length: 256 }, (_, v) => rangeColor(steps, v));

// [Doruk-Aksoy] The darkest and brightest texel over opaque RGB images.
export function luminosityRange(images) {
  let min = 255, max = 0;
  for (const rgb of images) {
    for (let i = 0; i < rgb.length; i += 3) {
      const l = luminance(rgb[i], rgb[i + 1], rgb[i + 2]);
      if (l < min) min = l;
      if (l > max) max = l;
    }
  }
  return { min, max };
}

// [Doruk-Aksoy] The [r, g, b] a texel of each brightness becomes, for a font whose texels range min..max.
export function luminosityTable(ramp, min, max) {
  if (max < min) { min = 0; max = 255; }
  const range = max - min + 1;
  return Array.from({ length: 256 }, (_, gray) => {
    const c = ramp[clamp255(Math.trunc(((gray - min) * 255) / range))];
    if (gray < min) return c.map((x) => Math.trunc((x * gray) / min));
    if (gray > max && max !== 0) return c.map((x) => clamp255(Math.trunc((x * gray) / max)));
    return c;
  });
}

// [Doruk-Aksoy] An RGB image with each texel recoloured to its brightness's table entry.
export function recolor(rgb, table) {
  const out = Buffer.from(rgb);
  for (let i = 0; i < out.length; i += 3) out.set(table[luminance(out[i], out[i + 1], out[i + 2])], i);
  return out;
}
