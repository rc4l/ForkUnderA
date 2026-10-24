import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { firefoxProfile, formatLogEntry, formatBidiEval, offScreenScript, smokeIn, smokeFirefox } from '../firefox.mjs';
import { smoke } from '../smoke.mjs';
import { scratch } from './scratch.mjs';

// [rc4l] Headless Firefox offers no WebGPU adapter, so its window opens off the screen and silent, and stays unthrottled there.
test('a fresh Firefox profile opens its window off the screen, silent and unthrottled', () => {
  const dir = scratch('ffprofile-');
  firefoxProfile(dir);
  const store = JSON.parse(fs.readFileSync(path.join(dir, 'xulstore.json'), 'utf8'));
  assert.equal(store['chrome://browser/content/browser.xhtml']['main-window'].screenX, '-4000');
  const prefs = fs.readFileSync(path.join(dir, 'user.js'), 'utf8');
  assert.match(prefs, /user_pref\("widget\.windows\.window_occlusion_tracking\.enabled", false\);/);
  assert.match(prefs, /user_pref\("media\.volume_scale", "0\.0"\);/);
  assert.match(prefs, /user_pref\("media\.autoplay\.default", 1\);/);
  firefoxProfile(dir, { autoplay: true });
  assert.match(fs.readFileSync(path.join(dir, 'user.js'), 'utf8'), /user_pref\("media\.autoplay\.default", 0\);/);
  fs.rmSync(dir, { recursive: true, force: true });
});

test('each browser gets its own driver, and a misspelt one stops the run', () => {
  assert.equal(smokeIn('chrome'), smoke);
  assert.equal(smokeIn('firefox'), smokeFirefox);
  assert.throws(() => smokeIn('ff'), /no driver for the browser "ff": chrome or firefox/);
});

test('BiDi log entries read as the console lines smoke gives', () => {
  assert.equal(formatLogEntry({ type: 'console', method: 'log', level: 'info', text: '[eval] 1' }), '[log] [eval] 1');
  assert.equal(formatLogEntry({ type: 'console', level: 'warn', text: 'careful' }), '[warn] careful');
  assert.equal(formatLogEntry({ type: 'javascript', level: 'error', text: 'TypeError: x is null' }), '[exception] TypeError: x is null');
});

test('a BiDi evaluation reads as the eval line a probe parses', () => {
  assert.equal(formatBidiEval({ type: 'success', result: { type: 'string', value: '{"said":[]}' } }), '[eval] {"said":[]}');
  assert.equal(formatBidiEval({ type: 'success', result: { type: 'undefined' } }), '[eval] null');
  assert.equal(formatBidiEval({ type: 'exception', exceptionDetails: { text: 'ReferenceError: Module is not defined' } }), '[eval] threw ReferenceError: Module is not defined');
  assert.equal(formatBidiEval(undefined), '[eval] threw unknown');
});

test('the move off the screen is for that one process', () => {
  const script = offScreenScript(4242);
  assert.match(script, /\$p -eq 4242 -and/);
  assert.match(script, /MoveWindow\(\$h, -4000, 40, 1280, 800, \$true\)/);
  assert.match(script, /"MOVED\|\$n"/);
});
