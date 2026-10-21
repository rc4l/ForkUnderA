import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { engineData, manifest, stage } from '../stage.mjs';
import { scratch } from './scratch.mjs';

test('the engine data is every pk3 in the build directory, in name order', () => {
  const dir = scratch();
  for (const f of ['zz.pk3', 'fua_core_k.PK3', 'forkundera.js', 'notes.txt']) fs.writeFileSync(path.join(dir, f), 'x');
  assert.deepEqual(engineData(dir).map((f) => path.basename(f)), ['fua_core_k.PK3', 'zz.pk3']);
});

test('the manifest names each file with its size', () => {
  const dir = scratch();
  fs.writeFileSync(path.join(dir, 'a.pk3'), 'abc');
  assert.deepEqual(manifest([path.join(dir, 'a.pk3')]), [{ name: 'a.pk3', size: 3 }]);
});

test('staging copies the module, the page and the data, with Freedoom and its licence', () => {
  const build = scratch(), out = path.join(scratch(), 'dist'), page = scratch(), freedoom = scratch();
  fs.writeFileSync(path.join(build, 'forkundera.js'), 'js');
  fs.writeFileSync(path.join(build, 'forkundera.wasm'), 'wasm');
  fs.writeFileSync(path.join(build, 'fua_core_k.pk3'), 'core');
  fs.writeFileSync(path.join(page, 'index.html'), '<p>');
  fs.writeFileSync(path.join(freedoom, 'freedoom2.wad'), 'iwad');
  fs.writeFileSync(path.join(freedoom, 'License.txt'), 'bsd');
  assert.deepEqual(stage({ buildDir: build, outDir: out, pageDir: page, freedoomDir: freedoom }), ['fua_core_k.pk3', 'freedoom2.wad']);
  assert.equal(fs.readFileSync(path.join(out, 'forkundera.wasm'), 'utf8'), 'wasm');
  assert.equal(fs.readFileSync(path.join(out, 'index.html'), 'utf8'), '<p>');
  assert.equal(fs.readFileSync(path.join(out, 'data', 'freedoom-license.txt'), 'utf8'), 'bsd');
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(out, 'data', 'files.json'), 'utf8')),
    [{ name: 'fua_core_k.pk3', size: 4 }, { name: 'freedoom2.wad', size: 4 }]);
});

test('staging without a build says to build first', () => {
  assert.throws(() => stage({ buildDir: scratch(), outDir: scratch(), pageDir: scratch(), freedoomDir: scratch() }), /build first/);
});

test('staging adds the wads it is given to the data and its manifest', () => {
  const build = scratch(), out = path.join(scratch(), 'dist'), page = scratch(), freedoom = scratch(), extra = scratch();
  for (const [dir, f] of [[build, 'forkundera.js'], [build, 'forkundera.wasm'], [freedoom, 'freedoom2.wad'], [freedoom, 'License.txt'], [extra, 'doom2.wad']]) fs.writeFileSync(path.join(dir, f), 'x');
  const staged = stage({ buildDir: build, outDir: out, pageDir: page, freedoomDir: freedoom, wads: [path.join(extra, 'doom2.wad')] });
  assert.deepEqual(staged, ['freedoom2.wad', 'doom2.wad']);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(out, 'data', 'files.json'), 'utf8')).map((f) => f.name), ['freedoom2.wad', 'doom2.wad']);
});
