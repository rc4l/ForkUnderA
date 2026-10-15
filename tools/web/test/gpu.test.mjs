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

test('a texture holds every mip of every layer at its format size, a compressed one by 4x4 blocks', () => {
  assert.equal(FuaGpu.textureBytes({ size: [4, 4], format: 'rgba8unorm' }), 64);
  assert.equal(FuaGpu.textureBytes({ size: { width: 4, height: 4, depthOrArrayLayers: 2 }, format: 'r8unorm', mipLevelCount: 3 }), (16 + 4 + 1) * 2);
  assert.equal(FuaGpu.textureBytes({ size: [4, 4, 4], format: 'rgba16float', dimension: '3d', mipLevelCount: 2 }), 4 * 4 * 4 * 8 + 2 * 2 * 2 * 8);
  assert.equal(FuaGpu.textureBytes({ size: [8, 5], format: 'bc1-rgba-unorm' }), 2 * 2 * 8);
  assert.equal(FuaGpu.textureBytes({ size: [8, 8], format: 'bc7-rgba-unorm' }), 4 * 16);
  assert.equal(FuaGpu.textureBytes({ size: [2, 2], format: 'depth24plus', sampleCount: 4 }), 64);
  assert.equal(FuaGpu.textureBytes({ format: 'unknown' }), 4);
});

// [rc4l] A device whose objects destroy themselves, and a registry the test collects by hand.
function fakeDevice() {
  const made = (d) => ({ d, destroyed: false, destroy() { this.destroyed = true; return 'gone'; } });
  return { createBuffer: made, createTexture: made };
}
class HandRegistry {
  constructor(onCollect) { this.onCollect = onCollect; this.held = new Map(); }
  register(target, value, token) { this.held.set(token, value); }
  unregister(token) { this.held.delete(token); }
  collect(token) { this.onCollect(this.held.get(token)); this.held.delete(token); }
}

test('the ledger holds what is made until it is destroyed or collected, and once only', () => {
  let registry;
  const device = fakeDevice();
  const ledger = FuaGpu.track(device, class extends HandRegistry { constructor(f) { super(f); registry = this; } });
  const buffer = device.createBuffer({ size: 100, label: 'fua scene VB' });
  const texture = device.createTexture({ size: [4, 4], format: 'rgba8unorm', label: 'fua material 12' });
  const empty = device.createBuffer({});
  assert.equal(ledger.heldBytes(), 164);
  assert.equal(ledger.heldObjects(), 3);
  assert.deepEqual(ledger.byTag(), [{ tag: 'fua scene VB', bytes: 100, objects: 1 }, { tag: 'fua material', bytes: 64, objects: 1 }, { tag: 'unnamed', bytes: 0, objects: 1 }]);
  assert.equal(ledger.tagText(), 'fua scene VB\t100\nfua material\t64\nunnamed\t0\n');
  assert.equal(buffer.destroy(), 'gone');
  assert.ok(buffer.destroyed);
  buffer.destroy();
  assert.equal(ledger.heldBytes(), 64);
  assert.equal(registry.held.size, 2);
  registry.collect(texture);
  assert.equal(ledger.heldBytes(), 0);
  empty.destroy();
  assert.equal(ledger.heldObjects(), 0);
  assert.deepEqual(ledger.byTag(), []);
});

test('a label is its tag without the number, list index or id that numbers its copies', () => {
  assert.equal(FuaGpu.tagOf('fua material 12'), 'fua material');
  assert.equal(FuaGpu.tagOf('Upload page #3'), 'Upload page');
  assert.equal(FuaGpu.tagOf('views[2]'), 'views');
  assert.equal(FuaGpu.tagOf(''), 'unnamed');
  assert.equal(FuaGpu.tagOf(undefined), 'unnamed');
});

test('without a finalization registry the ledger counts only what is destroyed', () => {
  const device = fakeDevice();
  const ledger = FuaGpu.track(device, null);
  device.createBuffer({ size: 8 }).destroy();
  device.createBuffer({ size: 8 });
  assert.equal(ledger.heldBytes(), 8);
});
