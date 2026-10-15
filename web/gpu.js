// [rc4l] The page's WebGPU device, asked of the adapter with everything the engine's shaders need.
'use strict';

(function (root) {
  // [rc4l] The optional features the engine uses where the adapter has them.
  const WANTED = ['depth-clip-control', 'depth32float-stencil8', 'texture-compression-bc', 'indirect-first-instance', 'float32-filterable'];

  // [rc4l] The device's descriptor: each wanted feature the adapter has, and every limit at the adapter's own, since the engine's shaders use more than WebGPU's defaults.
  function descriptor(adapter) {
    const requiredLimits = {};
    for (const k in adapter.limits) requiredLimits[k] = adapter.limits[k];
    return { requiredFeatures: WANTED.filter((f) => adapter.features.has(f)), requiredLimits };
  }

  // [rc4l] The adapter as a crash report names it: vendor, architecture and description, and whether it draws in software.
  function adapterName(info) {
    const i = info || {};
    return [i.vendor, i.architecture, i.description, i.isFallbackAdapter ? '(software)' : ''].filter(Boolean).join(' ');
  }

  // [rc4l] The device and its adapter's name from gpu (navigator.gpu), or a throw saying why there is none.
  async function makeDevice(gpu) {
    if (!gpu) throw new Error('This browser has no WebGPU. Chrome or Edge 113 and later, Safari 26, and Firefox on Windows have it.');
    const adapter = await gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('WebGPU is present but found no usable graphics adapter.');
    return { device: await adapter.requestDevice(descriptor(adapter)), adapter: adapterName(adapter.info) };
  }

  // [rc4l] Bytes per texel, or per 4x4 block of a compressed format, of the formats the engine makes; one not listed counts 4.
  const TEXEL_BYTES = { r8unorm: 1, r8snorm: 1, r8uint: 1, rg8unorm: 2, r16float: 2, r16uint: 2, depth16unorm: 2, rgba16float: 8, rg32float: 8, rgba16uint: 8, 'depth32float-stencil8': 8, rgba32float: 16, rgba32uint: 16 };
  const BLOCK_BYTES = { 'bc1-rgba-unorm': 8, 'bc1-rgba-unorm-srgb': 8, 'bc4-r-unorm': 8, 'bc4-r-snorm': 8 };

  // [rc4l] What a texture of this descriptor holds: every mip of every layer at its format's size, a 3D texture's depth halving with each mip.
  function textureBytes(d) {
    const s = d.size || {};
    const w = s.width ?? s[0] ?? 1, h = s.height ?? s[1] ?? 1, layers = s.depthOrArrayLayers ?? s[2] ?? 1;
    const block = /^(bc|etc2|eac|astc)/.test(d.format || '');
    const unit = block ? (BLOCK_BYTES[d.format] ?? 16) : (TEXEL_BYTES[d.format] ?? 4);
    let bytes = 0;
    for (let m = 0; m < (d.mipLevelCount ?? 1); m++) {
      const mw = Math.max(1, w >> m), mh = Math.max(1, h >> m), md = d.dimension === '3d' ? Math.max(1, layers >> m) : layers;
      bytes += (block ? Math.ceil(mw / 4) * Math.ceil(mh / 4) : mw * mh) * md * unit;
    }
    return bytes * (d.sampleCount ?? 1);
  }

  // [rc4l] An object's label as its tag, as the engine tags its own (ComputeMemoryTag): anything from a digit, '#' or '[' on dropped, so numbered copies share one.
  function tagOf(label) {
    return String(label || '').replace(/[\d#[].*$/, '').trim() || 'unnamed';
  }

  // [rc4l] Counts every buffer and texture device makes, by tag, until it is destroyed or collected, which a browser does to a released one only when its garbage collector runs; the engine reads heldBytes as gpu/process_bytes and tagText as gpu/page/.
  function track(device, Registry = globalThis.FinalizationRegistry) {
    let bytes = 0, objects = 0;
    const tags = new Map();
    const count = (tag, n, k) => {
      bytes += n; objects += k;
      const t = tags.get(tag) || { bytes: 0, objects: 0 };
      t.bytes += n; t.objects += k;
      tags.set(tag, t);
    };
    const collected = Registry ? new Registry(({ tag, n }) => count(tag, -n, -1)) : null;
    const wrap = (make, size) => function (d) {
      const made = make.call(this, d);
      const held = { tag: tagOf(d.label), n: size(d) };
      let live = true;
      count(held.tag, held.n, 1);
      if (collected) collected.register(made, held, made);
      const destroy = made.destroy;
      made.destroy = function () {
        if (live) { live = false; count(held.tag, -held.n, -1); if (collected) collected.unregister(made); }
        return destroy.call(this);
      };
      return made;
    };
    device.createBuffer = wrap(device.createBuffer, (d) => d.size || 0);
    device.createTexture = wrap(device.createTexture, textureBytes);
    // [rc4l] Each tag still holding an object, most bytes first.
    const byTag = () => [...tags].filter(([, t]) => t.objects > 0).map(([tag, t]) => ({ tag, bytes: t.bytes, objects: t.objects })).sort((a, b) => b.bytes - a.bytes);
    // [rc4l] The same as a line a tag, its name, a tab and its bytes, for the engine to read without parsing JSON.
    const tagText = () => byTag().map((t) => `${t.tag}\t${t.bytes}\n`).join('');
    return { heldBytes: () => bytes, heldObjects: () => objects, byTag, tagText };
  }

  const api = { WANTED, descriptor, adapterName, makeDevice, textureBytes, tagOf, track };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaGpu = api;
})(typeof window !== 'undefined' ? window : globalThis);
