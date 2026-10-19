import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import path from 'node:path';
import zlib from 'node:zlib';
import fs from 'node:fs';
import { stagedFiles, missingIsolation, fileProblems, redirectProblems, startProblems, browserProblems, verifySite } from '../demo/verify.mjs';
import { STARTED } from '../page.mjs';
import { ISOLATION_HEADERS } from '../headers.mjs';
import { scratch, scratchFiles } from './scratch.mjs';

const sha256 = (s) => crypto.createHash('sha256').update(s).digest('hex');

// [rc4l] A stand-in for the site: each path's status, headers and body, a 304 to an If-None-Match of its ETag unless revisit says otherwise, and every request kept.
function fakeSite(paths) {
  const asked = [];
  const fetch = async (url, init = {}) => {
    const p = new URL(url).pathname + new URL(url).search;
    const conditional = init.headers?.['If-None-Match'];
    asked.push(conditional ? `${p} if ${conditional}` : p);
    const r = paths[p];
    if (!r) return new Response('nope', { status: 404 });
    if (conditional && conditional === r.headers?.ETag) return new Response(null, { status: r.revisit ?? 304 });
    return new Response(r.body ?? null, { status: r.status ?? 200, headers: r.headers });
  };
  return { asked, fetch };
}

test('a stage lists each file at the path it is served from, a .gz by what it unpacks to', () => {
  const dir = scratchFiles({ 'demo/index.html': '<p>', 'demo/data/freedoom2.wad.gz': zlib.gzipSync('iwad') });
  assert.deepEqual(stagedFiles(dir), [
    { url: '/demo/data/freedoom2.wad', sha256: sha256('iwad') },
    { url: '/demo/index.html', sha256: sha256('<p>') },
  ]);
  assert.throws(() => stagedFiles(path.join(scratch(), 'none')), /stage the site first/);
});

test('a response missing an isolation header is named', () => {
  assert.deepEqual(missingIsolation(new Response(null, { headers: ISOLATION_HEADERS })), []);
  const { 'Cross-Origin-Embedder-Policy': _, ...rest } = ISOLATION_HEADERS;
  assert.deepEqual(missingIsolation(new Response(null, { headers: rest })), ['Cross-Origin-Embedder-Policy']);
});

test('a file served isolated, with the staged bytes and a 304 for a revisit, has no problems', async () => {
  const site = fakeSite({ '/demo/a.js': { body: 'js', headers: { ...ISOLATION_HEADERS, ETag: '"1"' } } });
  assert.deepEqual(await fileProblems('https://x', { url: '/demo/a.js', sha256: sha256('js') }, site.fetch), []);
  assert.deepEqual(site.asked, ['/demo/a.js', '/demo/a.js if "1"']);
});

// [rc4l] The first deploy sent a 404 to every revisit of freedoom2.wad, which only a second visit showed.
test('each way a file can be wrong is a problem', async () => {
  const file = { url: '/demo/a.js', sha256: sha256('js') };
  const check = (r) => fileProblems('https://x', file, fakeSite({ '/demo/a.js': r }).fetch);
  assert.deepEqual(await check({ status: 500 }), ['/demo/a.js: 500']);
  assert.deepEqual(await fileProblems('https://x', file, fakeSite({}).fetch), ['/demo/a.js: 404']);
  assert.deepEqual(await check({ body: 'old', headers: { ...ISOLATION_HEADERS, ETag: '"1"' } }), ['/demo/a.js: not the staged bytes']);
  assert.deepEqual(await check({ body: 'js', headers: ISOLATION_HEADERS }), ['/demo/a.js: no ETag, so every visit downloads it again']);
  assert.deepEqual(await check({ body: 'js', headers: { ...ISOLATION_HEADERS, 'Content-Type': 'text/html' } }), []);
  const empty = { url: '/demo/e', sha256: sha256('') };
  assert.deepEqual(await fileProblems('https://x', empty, fakeSite({ '/demo/e': { headers: ISOLATION_HEADERS } }).fetch), ['/demo/e: no ETag, so every visit downloads it again']);
  assert.deepEqual(await check({ body: 'js', headers: { ...ISOLATION_HEADERS, ETag: '"1"' }, revisit: 404 }), ['/demo/a.js: a revisit got 404, not 304']);
  assert.deepEqual(await check({ body: 'js', headers: { ETag: '"1"' } }), Object.keys(ISOLATION_HEADERS).map((k) => `/demo/a.js: no ${k}`));
});

test('the bare address must redirect to the page keeping the query', async () => {
  const at = (status, Location) => fakeSite({ '/?map=MAP01': { status, headers: Location ? { Location } : {} } }).fetch;
  assert.deepEqual(await redirectProblems('https://x', at(302, '/demo/?map=MAP01')), []);
  assert.deepEqual(await redirectProblems('https://x', at(302, '/demo/')), ['/: 302 to /demo/, not 302 to /demo/?map=MAP01']);
  assert.deepEqual(await redirectProblems('https://x', at(200)), ['/: 200 to null, not 302 to /demo/?map=MAP01']);
});

test('a browser visit is judged by what STARTED printed', () => {
  assert.deepEqual(startProblems('revisit', '[eval] {"isolated":true,"error":null}'), []);
  assert.deepEqual(startProblems('revisit', '[eval] {"isolated":true,"error":"Could not fetch freedoom2.wad (404)"}'), ['revisit: the page showed "Could not fetch freedoom2.wad (404)"']);
  assert.deepEqual(startProblems('first visit', '[eval] {"isolated":false,"error":null}'), ['first visit: the page is not cross-origin isolated']);
  assert.deepEqual(startProblems('first visit', '[eval] threw TypeError: x'), ['first visit: [eval] threw TypeError: x']);
  assert.deepEqual(startProblems('first visit', null), ['first visit: the page neither started nor showed an error']);
});

// [rc4l] A stand-in for smoke(): each visit's STARTED line in turn, with every run's options kept.
function fakeRun(lines) {
  const runs = [];
  return { runs, run: async (opts) => { opts.onLine('[log] a line'); runs.push({ ...opts, profileExisted: fs.existsSync(opts.profile) }); return { matched: lines[runs.length - 1] }; } };
}

test('the browser visits twice on one profile, the second as a returning player, and the profile goes after', async () => {
  const ok = '[eval] {"isolated":true,"error":null}';
  const fake = fakeRun([ok, '[eval] {"isolated":true,"error":"Could not fetch freedoom2.wad (404)"}']);
  assert.deepEqual(await browserProblems('https://x', fake.run), ['revisit: the page showed "Could not fetch freedoom2.wad (404)"']);
  assert.equal(fake.runs.length, 2);
  assert.equal(fake.runs[0].profile, fake.runs[1].profile);
  assert.ok(fake.runs.every((r) => r.profileExisted && r.url === 'https://x/?map=MAP01' && r.evaluate === STARTED));
  assert.equal(fs.existsSync(fake.runs[0].profile), false);
});

test('a site check sums up its problems, or what it found as staged', async () => {
  const dir = scratchFiles({ 'demo/a.js': 'js' });
  const site = fakeSite({ '/?map=MAP01': { status: 302, headers: { Location: '/demo/?map=MAP01' } }, '/demo/a.js': { body: 'js', headers: { ...ISOLATION_HEADERS, ETag: '"1"' } } });
  const ok = '[eval] {"isolated":true,"error":null}';
  assert.deepEqual(await verifySite({ base: 'https://x', dir, fetch: site.fetch, run: fakeRun([ok, ok]).run }),
    { problems: [], summary: 'https://x: the redirect, every staged file (1) and its revisit, and two browser visits, are as staged' });
  assert.deepEqual(await verifySite({ base: 'https://x', dir, browser: false, fetch: site.fetch }),
    { problems: [], summary: 'https://x: the redirect, every staged file (1) and its revisit are as staged' });
  const broken = await verifySite({ base: 'https://x', dir, browser: false, fetch: fakeSite({}).fetch });
  assert.deepEqual(broken, { problems: ['/: 404 to null, not 302 to /demo/?map=MAP01', '/demo/a.js: 404'], summary: 'https://x: 2 problems' });
});
