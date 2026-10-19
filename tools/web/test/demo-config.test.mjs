import { test } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { DEMO_DIR, parseJsonc, readConfig, siteDir, siteUrl } from '../demo/config.mjs';

test('a JSONC file reads with its comment lines left out', () => {
  assert.deepEqual(parseJsonc('// a comment\n{\n  // another\n  "a": 1\n}\n'), { a: 1 });
  assert.throws(() => parseJsonc('{ "a": 1 } // trailing'), SyntaxError);
});

test('the site folder and address come from the deploy\'s own config', () => {
  const config = readConfig();
  assert.equal(siteDir(config), path.resolve(DEMO_DIR, config.assets.directory));
  assert.match(siteUrl(config), /^https:\/\/[a-z0-9.-]+$/);
});

test('a config with no custom domain says so', () => {
  assert.equal(siteUrl({ routes: [{ pattern: 'a.example/*' }, { pattern: 'b.example', custom_domain: true }] }), 'https://b.example');
  assert.throws(() => siteUrl({ routes: [{ pattern: 'a.example/*' }] }), /no custom domain/);
  assert.throws(() => siteUrl({}), /no custom domain/);
});
