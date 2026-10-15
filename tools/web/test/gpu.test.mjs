import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const FuaGpu = createRequire(import.meta.url)('../../../web/gpu.js');

const adapter = (features, info) => ({
  features: new Set(features),
  limits: { maxBindGroups: 8, maxTextureDimension2D: 16384 },
  info,
  requestDevice: async (d) => ({ asked: d }),
});

test('the device asks for the wanted features the adapter has and every limit at its own', () => {
  const d = FuaGpu.descriptor(adapter(['depth-clip-control', 'shader-f16', 'float32-filterable']));
  assert.deepEqual(d.requiredFeatures, ['depth-clip-control', 'float32-filterable']);
  assert.deepEqual(d.requiredLimits, { maxBindGroups: 8, maxTextureDimension2D: 16384 });
});

test('an adapter is named by what it says of itself, and a software one says so', () => {
  assert.equal(FuaGpu.adapterName({ vendor: 'v', architecture: 'a', description: '', isFallbackAdapter: true }), 'v a (software)');
  assert.equal(FuaGpu.adapterName(undefined), '');
});

test('a device comes with its adapter named, and a browser without one says why', async () => {
  const gpu = { requestAdapter: async () => adapter(['depth-clip-control'], { vendor: 'v' }) };
  const made = await FuaGpu.makeDevice(gpu);
  assert.equal(made.adapter, 'v');
  assert.deepEqual(made.device.asked.requiredFeatures, ['depth-clip-control']);
  await assert.rejects(FuaGpu.makeDevice(undefined), /no WebGPU/);
  await assert.rejects(FuaGpu.makeDevice({ requestAdapter: async () => null }), /no usable graphics adapter/);
});
