import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { stageDemo } from '../demo/stage.mjs';
import { scratch, scratchFiles } from './scratch.mjs';

test('the stage puts the page under demo/ and gzips only what is over the limit', () => {
  const big = Buffer.alloc(64, 7);
  const distDir = scratchFiles({ 'index.html': '<p>', 'data/freedoom2.wad': big, 'data/small.pk3': 'pk3', 'data/z.bin': big });
  const outDir = path.join(scratch(), 'site');
  assert.deepEqual(stageDemo({ distDir, outDir, limit: 32 }), ['data/freedoom2.wad', 'data/z.bin']);
  assert.equal(fs.readFileSync(path.join(outDir, 'demo', 'index.html'), 'utf8'), '<p>');
  assert.equal(fs.readFileSync(path.join(outDir, 'demo', 'data', 'small.pk3'), 'utf8'), 'pk3');
  assert.equal(fs.existsSync(path.join(outDir, 'demo', 'data', 'freedoom2.wad')), false);
  assert.deepEqual(zlib.gunzipSync(fs.readFileSync(path.join(outDir, 'demo', 'data', 'freedoom2.wad.gz'))), big);
});

test('a second stage replaces the first, leaving nothing of it behind', () => {
  const outDir = path.join(scratch(), 'site');
  stageDemo({ distDir: scratchFiles({ 'index.html': 'one', 'old.js': 'x' }), outDir });
  assert.deepEqual(stageDemo({ distDir: scratchFiles({ 'index.html': 'two' }), outDir }), []);
  assert.deepEqual(fs.readdirSync(path.join(outDir, 'demo')), ['index.html']);
  assert.equal(fs.readFileSync(path.join(outDir, 'demo', 'index.html'), 'utf8'), 'two');
});

test('the stage refuses a page that is not built, a folder it did not make, and a file too big even gzipped', () => {
  assert.throws(() => stageDemo({ distDir: scratch(), outDir: path.join(scratch(), 'site') }), /stage the web build first/);
  const theirs = scratchFiles({ 'keep.txt': 'mine' });
  assert.throws(() => stageDemo({ distDir: scratchFiles({ 'index.html': '<p>' }), outDir: theirs }), /refusing to empty it/);
  assert.equal(fs.readFileSync(path.join(theirs, 'keep.txt'), 'utf8'), 'mine');
  const noise = Buffer.from(Array.from({ length: 256 }, (_, i) => (i * 97 + 13) % 251));
  assert.throws(() => stageDemo({ distDir: scratchFiles({ 'index.html': '<p>', 'n.bin': noise }), outDir: path.join(scratch(), 'site'), limit: 64 }), /over the asset limit even gzipped/);
});
