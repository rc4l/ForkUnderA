import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const FuaCrash = createRequire(import.meta.url)('../../../web/crashreport.js');

const CHROME = `RangeError: Failed to execute 'createBuffer' on 'GPUDevice': too large
    at _wgpuDeviceCreateBuffer (http://127.0.0.1:45211/forkundera.js:1:341016)
    at zx::hwrender::BuildSkyDome(SkyDome&, int, int) (http://127.0.0.1:45211/forkundera.wasm:wasm-function[9108]:0x777285)
    at http://127.0.0.1:45211/forkundera.wasm:wasm-function[8981]:0x75658d
    at <anonymous>`;
const FIREFOX = `_wgpuDeviceCreateBuffer@https://example.org/play/forkundera.js:1:341016
@https://example.org/play/forkundera.wasm:wasm-function[8981]:0x75658d
not a frame`;

const CONTEXT = { release: 'ForkUnderA@v0.3.2', dist: 'abc123', dsn: 'https://key@crash.example.org/1', reporting: true, map: 'map01', iwad: 'freedoom2.wad', files: ['fua_core.pk3', 'freedoom2.wad'], tags: { build: 'client', renderer: 'webgpu' } };
const NOW = new Date('2026-10-14T12:00:00Z');

test('a DSN names its envelope endpoint, and anything else names none', () => {
  assert.equal(FuaCrash.envelopeUrl('https://key@crash.example.org/1'), 'https://crash.example.org/api/1/envelope/?sentry_key=key&sentry_version=7');
  assert.equal(FuaCrash.envelopeUrl('http://k@127.0.0.1:9000/12/'), 'http://127.0.0.1:9000/api/12/envelope/?sentry_key=k&sentry_version=7');
  assert.equal(FuaCrash.envelopeUrl(''), null);
  assert.equal(FuaCrash.envelopeUrl('https://crash.example.org/1'), null);
  assert.equal(FuaCrash.envelopeUrl('https://key@crash.example.org/project'), null);
});

test("Chrome's stack becomes frames oldest first, each file a path", () => {
  const frames = FuaCrash.parseStack(CHROME);
  assert.deepEqual(frames, [
    { filename: '<anonymous>', in_app: false, function: '?' },
    { filename: '/forkundera.wasm', in_app: true, instruction_addr: '0x75658d', function: 'wasm-function[8981]' },
    { filename: '/forkundera.wasm', in_app: true, instruction_addr: '0x777285', function: 'zx::hwrender::BuildSkyDome(SkyDome&, int, int)' },
    { filename: '/forkundera.js', in_app: true, lineno: 1, colno: 341016, function: '_wgpuDeviceCreateBuffer' },
  ]);
});

test("Firefox's stack reads the same way, and lines that are no frame are skipped", () => {
  const frames = FuaCrash.parseStack(FIREFOX);
  assert.equal(frames.length, 2);
  assert.deepEqual(frames[0], { filename: '/play/forkundera.wasm', in_app: true, instruction_addr: '0x75658d', function: 'wasm-function[8981]' });
  assert.equal(frames[1].function, '_wgpuDeviceCreateBuffer');
  assert.deepEqual(FuaCrash.parseStack(undefined), []);
});

test('an event id is 32 hex digits', () => {
  assert.match(FuaCrash.eventId(), /^[0-9a-f]{32}$/);
  assert.equal(FuaCrash.eventId(() => 0.999), 'f'.repeat(32));
});

test('a crash is a fatal event carrying the game, the browser and the console', () => {
  const log = Array.from({ length: 150 }, (_, i) => `line ${i}`);
  const e = FuaCrash.buildEvent({ kind: 'exception', message: 'boom', stack: CHROME, context: CONTEXT, browser: 'UA', adapter: 'vendor arch', log, now: NOW, id: 'a'.repeat(32) });
  assert.equal(e.event_id, 'a'.repeat(32));
  assert.equal(e.timestamp, '2026-10-14T12:00:00.000Z');
  assert.equal(e.level, 'fatal');
  assert.equal(e.logger, 'exception');
  assert.equal(e.release, 'ForkUnderA@v0.3.2');
  assert.equal(e.dist, 'abc123');
  assert.deepEqual(e.tags, { map: 'map01', iwad: 'freedoom2.wad', build: 'web', renderer: 'webgpu', kind: 'exception' });
  assert.equal(e.extra.load_order, 'fua_core.pk3, freedoom2.wad');
  assert.equal(e.extra.log.split('\n').length, 100);
  assert.match(e.extra.log, /^line 50\n/);
  assert.deepEqual(e.contexts, { browser: { name: 'UA' }, gpu: { name: 'vendor arch' } });
  assert.equal(e.exception.values[0].stacktrace.frames.length, 4);
  assert.equal(e.fingerprint, undefined);
});

test('a fatal or a device error groups on its message, and only a device error is not fatal', () => {
  const fatal = FuaCrash.buildEvent({ kind: 'fatal', message: 'Bad WAD', now: NOW, id: 'b' });
  assert.deepEqual(fatal.fingerprint, ['fatal', 'Bad WAD']);
  assert.equal(fatal.level, 'fatal');
  assert.equal(fatal.release, undefined);
  assert.deepEqual(fatal.tags, { map: undefined, iwad: undefined, build: 'web', kind: 'fatal' });
  const gpu = FuaCrash.buildEvent({ kind: 'webgpu', message: 'Invalid BindGroup', now: NOW, id: 'c' });
  assert.equal(gpu.level, 'error');
  assert.deepEqual(gpu.fingerprint, ['webgpu', 'Invalid BindGroup']);
  assert.equal(FuaCrash.buildEvent({ kind: 'abort', message: 'x', id: 'd' }).fingerprint, undefined);
});

test('an envelope is its header, the item header and the event, a line each', () => {
  const e = FuaCrash.buildEvent({ kind: 'fatal', message: 'Bad WAD', now: NOW, id: 'e'.repeat(32) });
  const lines = FuaCrash.envelope(e, CONTEXT.dsn, NOW).split('\n');
  assert.equal(lines.length, 3);
  assert.deepEqual(JSON.parse(lines[0]), { event_id: 'e'.repeat(32), sent_at: '2026-10-14T12:00:00.000Z', dsn: CONTEXT.dsn });
  assert.deepEqual(JSON.parse(lines[1]), { type: 'event' });
  assert.equal(JSON.parse(lines[2]).message.formatted, 'Bad WAD');
});

test('a report is sent only from a build with a DSN, with reports on, and not past the limit', () => {
  assert.equal(FuaCrash.decision(CONTEXT, 0), 'send');
  assert.equal(FuaCrash.decision(CONTEXT, FuaCrash.MAX_SENT), 'limit');
  assert.equal(FuaCrash.decision({ ...CONTEXT, reporting: false }, 0), 'off');
  assert.equal(FuaCrash.decision({ ...CONTEXT, dsn: '' }, 0), 'unofficial');
  assert.equal(FuaCrash.decision({}, 0), 'unofficial');
  assert.equal(FuaCrash.decision(null, 0), 'unofficial');
});

test('the page says what became of a report', () => {
  assert.equal(FuaCrash.statusText('send', true), 'The report was sent to the developers.');
  assert.match(FuaCrash.statusText('send', false), /could not be sent/);
  assert.match(FuaCrash.statusText('off', false), /cl_crashreports 0/);
  assert.match(FuaCrash.statusText('limit', false), /Enough reports/);
  assert.match(FuaCrash.statusText('unofficial', false), /does not send/);
});

test('the text a person copies names the crash, the game and the stack newest first', () => {
  const e = FuaCrash.buildEvent({ kind: 'exception', message: 'boom', stack: CHROME, context: CONTEXT, browser: 'UA', adapter: 'gpu', log: ['last line'], now: NOW, id: 'f'.repeat(32) });
  const text = FuaCrash.reportText(e).split('\n');
  assert.equal(text[0], `ForkUnderA crash report ${'f'.repeat(32)}`);
  assert.ok(text.includes('What: exception: boom'));
  assert.ok(text.includes('Release: ForkUnderA@v0.3.2 (abc123)'));
  assert.ok(text.includes('Map: map01'));
  assert.ok(text.includes('Files: fua_core.pk3, freedoom2.wad'));
  const stack = text.indexOf('Stack:');
  assert.equal(text[stack + 1], '  _wgpuDeviceCreateBuffer (/forkundera.js:1:341016)');
  assert.equal(text[stack + 2], '  zx::hwrender::BuildSkyDome(SkyDome&, int, int) (/forkundera.wasm:0x777285)');
  assert.equal(text[stack + 4], '  ? (<anonymous>)');
  assert.equal(text[text.length - 1], 'last line');
  const bare = FuaCrash.reportText(FuaCrash.buildEvent({ kind: 'fatal', message: 'x', now: NOW, id: '0' })).split('\n');
  assert.ok(bare.includes('Release: unknown'));
  assert.ok(bare.includes('Map: none'));
  assert.ok(bare.includes('Files: none'));
  assert.ok(bare.includes('  (none)'));
});

test('the reporter sends when it may, keeps the last report and stops at the limit', async () => {
  const sent = [];
  const r = FuaCrash.reporter({ send: async (url, body) => { sent.push({ url, body }); return true; }, browser: 'UA', log: () => ['a', 'b'] });
  assert.equal(r.last(), null);
  r.setContext(JSON.stringify(CONTEXT));
  assert.deepEqual(r.context(), CONTEXT);
  r.setAdapter('gpu');
  const first = await r.report('fatal', 'Bad WAD', '');
  assert.equal(first.why, 'send');
  assert.equal(first.delivered, true);
  assert.equal(first.status, 'The report was sent to the developers.');
  assert.equal(r.last(), first);
  assert.equal(sent[0].url, FuaCrash.envelopeUrl(CONTEXT.dsn));
  const event = JSON.parse(sent[0].body.split('\n')[2]);
  assert.equal(event.contexts.gpu.name, 'gpu');
  assert.equal(event.extra.log, 'a\nb');
  for (let i = 1; i < FuaCrash.MAX_SENT; i++) await r.report('fatal', 'again', '');
  assert.equal((await r.report('fatal', 'one too many', '')).why, 'limit');
  assert.equal(sent.length, FuaCrash.MAX_SENT);
});

test('a send that fails or throws leaves the report to copy, and an object context is taken as it is', async () => {
  const failing = FuaCrash.reporter({ send: async () => { throw new Error('offline'); } });
  failing.setContext(CONTEXT);
  assert.equal((await failing.report('fatal', 'x', '')).delivered, false);
  const unofficial = FuaCrash.reporter({ send: async () => assert.fail('an unofficial build sent a report') });
  const result = await unofficial.report('exception', 'x', CHROME);
  assert.equal(result.why, 'unofficial');
  assert.match(result.text, /^ForkUnderA crash report /);
});

test('a report goes out as plain text that outlives the page', async () => {
  const calls = [];
  const saved = globalThis.fetch;
  globalThis.fetch = async (url, init) => { calls.push({ url, init }); return { ok: true }; };
  try {
    assert.equal(await FuaCrash.fetchSend('https://host/api/1/envelope/', 'body'), true);
  } finally {
    globalThis.fetch = saved;
  }
  assert.deepEqual(calls[0], { url: 'https://host/api/1/envelope/', init: { method: 'POST', body: 'body', keepalive: true, headers: { 'Content-Type': 'text/plain;charset=UTF-8' } } });
});
