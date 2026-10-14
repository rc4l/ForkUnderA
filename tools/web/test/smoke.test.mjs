import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { chromeArgs, downloadsSettled, formatConsole, formatEval } from '../smoke.mjs';

test('Chrome runs headless with WebGPU on the given port and profile', () => {
  const args = chromeArgs(9333, '/tmp/p');
  assert.ok(args.includes('--headless=new'));
  assert.ok(args.includes('--remote-debugging-port=9333'));
  assert.ok(args.includes('--user-data-dir=/tmp/p'));
  assert.ok(args.includes('--enable-unsafe-webgpu'));
  assert.ok(!chromeArgs(9333, '/tmp/p', { headless: false }).includes('--headless=new'));
  assert.ok(chromeArgs(9333, '/tmp/p', { software: true }).includes('--use-webgpu-adapter=swiftshader'));
  assert.ok(!chromeArgs(9333, '/tmp/p').includes('--use-webgpu-adapter=swiftshader'));
});

test('console calls, exceptions and log entries each become one line', () => {
  assert.equal(formatConsole('Runtime.consoleAPICalled', { type: 'log', args: [{ value: 'a' }, { description: 'b' }, { type: 'undefined' }] }), '[log] a b undefined');
  assert.equal(formatConsole('Runtime.exceptionThrown', { exceptionDetails: { exception: { description: 'Error: x' } } }), '[exception] Error: x');
  assert.equal(formatConsole('Runtime.exceptionThrown', { exceptionDetails: { text: 'Uncaught' } }), '[exception] Uncaught');
  assert.equal(formatConsole('Runtime.exceptionThrown', {}), '[exception] unknown');
  assert.equal(formatConsole('Log.entryAdded', { entry: { level: 'warning', text: 'w' } }), '[warning] w');
  assert.equal(formatConsole('Page.loadEventFired', {}), null);
});

test('an evaluation prints its value as JSON, or what it threw', () => {
  assert.equal(formatEval({ result: { value: { a: 1 } } }), '[eval] {"a":1}');
  assert.equal(formatEval({ result: { type: 'undefined' } }), '[eval] null');
  assert.equal(formatEval(undefined), '[eval] null');
  assert.equal(formatEval({ exceptionDetails: { text: 'Uncaught', exception: { description: 'ReferenceError: x' } } }), '[eval] threw ReferenceError: x');
  assert.equal(formatEval({ exceptionDetails: { text: 'Uncaught' } }), '[eval] threw Uncaught');
});

test('a download has settled once a file is there and none is still being written', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-dl-'));
  assert.equal(await downloadsSettled(dir, { timeoutMs: 30, pollMs: 5 }), false);
  fs.writeFileSync(path.join(dir, 'shot.png.crdownload'), 'x');
  assert.equal(await downloadsSettled(dir, { timeoutMs: 30, pollMs: 5 }), false);
  setTimeout(() => fs.renameSync(path.join(dir, 'shot.png.crdownload'), path.join(dir, 'shot.png')), 20);
  assert.equal(await downloadsSettled(dir, { timeoutMs: 2000, pollMs: 5 }), true);
  fs.rmSync(dir, { recursive: true, force: true });
});
