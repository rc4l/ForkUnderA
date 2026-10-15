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

  const api = { WANTED, descriptor, adapterName, makeDevice };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaGpu = api;
})(typeof window !== 'undefined' ? window : globalThis);
