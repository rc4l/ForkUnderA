import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { chromeArgs, devToolsPort, downloadsSettled, formatConsole, formatEval, gpuLogLines, inputAsked, removeQuietly, retry } from '../smoke.mjs';

test("Chrome's log is read for what it says about its GPU process, not for the page's console", () => {
  const log = [
    '[1:2:INFO:CONSOLE(17)] "vulkan: uploaded 18948 verts", source: fua.js (17)',
    '[1:2:INFO:CONSOLE:20] "vulkan sky: SKY1", source: fua.js (20)',
    '[1:2:ERROR:gpu_process_host.cc(1000)] GPU process exited unexpectedly: exit_code=139',
    '[1:2:INFO:other.cc(1)] nothing to see',
    '[1:2:WARNING:dawn_context.cc(5)] Device lost',
  ].join('\n');
  assert.deepEqual(gpuLogLines(log), [
    '[1:2:ERROR:gpu_process_host.cc(1000)] GPU process exited unexpectedly: exit_code=139',
    '[1:2:WARNING:dawn_context.cc(5)] Device lost',
  ]);
  assert.deepEqual(gpuLogLines(log, 1), ['[1:2:WARNING:dawn_context.cc(5)] Device lost']);
});

test('Chrome runs headless with WebGPU on the given port and profile', () => {
  const args = chromeArgs(9333, '/tmp/p');
  assert.ok(args.includes('--headless=new'));
  assert.ok(args.includes('--remote-debugging-port=9333'));
  assert.ok(args.includes('--user-data-dir=/tmp/p'));
  assert.ok(args.includes('--enable-unsafe-webgpu'));
  assert.ok(args.includes('--js-flags=--expose-gc'));
  assert.ok(!chromeArgs(9333, '/tmp/p', { headless: false }).includes('--headless=new'));
  assert.ok(chromeArgs(9333, '/tmp/p', { software: true }).includes('--use-webgpu-adapter=swiftshader'));
  assert.ok(chromeArgs(9333, '/tmp/p', { software: true, platform: 'linux' }).includes('--use-vulkan=swiftshader'));
  assert.ok(!chromeArgs(9333, '/tmp/p', { software: true, platform: 'win32' }).includes('--use-vulkan=swiftshader'));
  assert.ok(!chromeArgs(9333, '/tmp/p', { platform: 'linux' }).includes('--use-vulkan=swiftshader'));
  assert.ok(!chromeArgs(9333, '/tmp/p').includes('--use-webgpu-adapter=swiftshader'));
  assert.ok(args.includes('--mute-audio'));
  assert.ok(!args.includes('--autoplay-policy=no-user-gesture-required'));
  assert.ok(chromeArgs(9333, '/tmp/p', { autoplay: true }).includes('--autoplay-policy=no-user-gesture-required'));
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

test('a page asks for a click or a key it knows with a console line', () => {
  assert.deepEqual(inputAsked('[log] check: click'), { click: true });
  assert.deepEqual(inputAsked('[log] check: press Tab'), { key: 'Tab', code: 'Tab', keyCode: 9 });
  assert.equal(inputAsked('[log] check: press F13'), null);
  assert.equal(inputAsked('[log] something else'), null);
  assert.deepEqual(inputAsked('[log] check: resize 900x700'), { resize: [900, 700] });
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

test('a scratch folder is removed, and one a process still holds is reported rather than thrown', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-remove-'));
  fs.writeFileSync(path.join(dir, 'f'), 'x');
  assert.equal(removeQuietly(dir), true);
  assert.equal(fs.existsSync(dir), false);
  const said = [];
  const busy = () => { const e = new Error('directory not empty'); e.code = 'ENOTEMPTY'; throw e; };
  assert.equal(removeQuietly('/tmp/held', busy, (m) => said.push(m)), false);
  assert.deepEqual(said, ['left /tmp/held behind: ENOTEMPTY']);
});

// [rc4l] A headless Chrome left running by an earlier check held port 9333, and every later check drove its stale tab and read its old console.
test('a run reads the debugging port its own Chrome chose, waiting until it is written', async () => {
  const reads = ['', 'not a port', '61234\n/devtools/browser/abc'];
  const port = await devToolsPort('/p', Date.now() + 1000, { read: () => { const r = reads.shift(); if (r === '') throw new Error('ENOENT'); return r; }, wait: async () => {} });
  assert.equal(port, 61234);
  await assert.rejects(devToolsPort('/p', Date.now() - 1, { read: () => { throw new Error('ENOENT'); }, wait: async () => {} }), /wrote no DevToolsActivePort/);
});

test('a retry returns the first answer, and past its deadline throws the last error or a timeout', async () => {
  const answers = [undefined, 'no', 7];
  let tries = 0;
  assert.equal(await retry(() => { tries++; const a = answers.shift(); if (a === 'no') throw new Error('not yet'); return a; }, Date.now() + 1000, async () => {}), 7);
  assert.equal(tries, 3);
  await assert.rejects(retry(() => { throw new Error('refused'); }, Date.now() - 1, async () => {}), /refused/);
  await assert.rejects(retry(() => undefined, Date.now() - 1, async () => {}), /timed out/);
});
