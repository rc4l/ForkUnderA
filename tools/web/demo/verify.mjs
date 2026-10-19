#!/usr/bin/env node
// [rc4l] Checks the deployed demo site against the stage it was deployed from, as a player meets it: the bare address, every file and a revisit of it, and the page started twice on one browser profile.
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { ISOLATION_HEADERS } from '../headers.mjs';
import { STARTED } from '../page.mjs';
import { smoke, removeQuietly } from '../smoke.mjs';
import { DEMO_PATH } from './respond.mjs';
import { readConfig, siteDir, siteUrl } from './config.mjs';

const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

// [rc4l] Each file a stage holds, as the path the site serves it at and the sha256 of what a browser ends up with, a .gz's unpacked.
export function stagedFiles(dir) {
  if (!fs.existsSync(dir)) throw new Error(`no ${dir}: stage the site first`);
  return fs.readdirSync(dir, { recursive: true }).map((rel) => rel.split(path.sep).join('/')).filter((rel) => fs.statSync(path.join(dir, rel)).isFile()).sort().map((rel) => {
    const bytes = fs.readFileSync(path.join(dir, rel));
    const gz = rel.endsWith('.gz');
    return { url: '/' + (gz ? rel.slice(0, -3) : rel), sha256: sha256(gz ? zlib.gunzipSync(bytes) : bytes) };
  });
}

// [rc4l] The isolation headers res lacks, by name.
export function missingIsolation(res) {
  return Object.entries(ISOLATION_HEADERS).filter(([k, v]) => res.headers.get(k) !== v).map(([k]) => k);
}

// [rc4l] What is wrong with the site's answers for one staged file, a line each: its status, its isolation, its bytes, and the 304 a revisit's If-None-Match should get.
export async function fileProblems(base, file, fetch = globalThis.fetch) {
  const res = await fetch(base + file.url);
  if (res.status !== 200) return [`${file.url}: ${res.status}`];
  const problems = missingIsolation(res).map((k) => `${file.url}: no ${k}`);
  if (sha256(Buffer.from(await res.arrayBuffer())) !== file.sha256) problems.push(`${file.url}: not the staged bytes`);
  const etag = res.headers.get('ETag');
  // [rc4l] Cloudflare's edge drops an HTML page's ETag when the zone's HTML rewriting features are on, which costs a revisit only the page's own few KB.
  if (!etag && (res.headers.get('Content-Type') ?? '').startsWith('text/html')) return problems;
  const again = etag ? await fetch(base + file.url, { headers: { 'If-None-Match': etag } }) : null;
  if (!again) problems.push(`${file.url}: no ETag, so every visit downloads it again`);
  else if (again.status !== 304) problems.push(`${file.url}: a revisit got ${again.status}, not 304`);
  return problems;
}

// [rc4l] What is wrong with the bare address, which must send a visitor to the page with their query.
export async function redirectProblems(base, fetch = globalThis.fetch) {
  const res = await fetch(base + '/?map=MAP01', { redirect: 'manual' });
  const want = DEMO_PATH + '?map=MAP01';
  const got = res.headers.get('Location');
  return res.status === 302 && got === want ? [] : [`/: ${res.status} to ${got}, not 302 to ${want}`];
}

// [rc4l] What is wrong with one browser visit, from the line STARTED's result printed (or none, if it never did).
export function startProblems(visit, line) {
  if (!line) return [`${visit}: the page neither started nor showed an error`];
  if (line.startsWith('[eval] threw')) return [`${visit}: ${line}`];
  const { isolated, error } = JSON.parse(line.slice('[eval] '.length));
  return [...(error ? [`${visit}: the page showed "${error}"`] : []), ...(isolated ? [] : [`${visit}: the page is not cross-origin isolated`])];
}

// [rc4l] The page started twice on one profile by run (smoke.mjs's smoke), the second from the browser's cache as a returning player's is.
export async function browserProblems(base, run = smoke) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fua-verify-'));
  const problems = [];
  try {
    for (const visit of ['first visit', 'revisit']) {
      const { matched } = await run({ url: `${base}/?map=MAP01`, evaluate: STARTED, until: '^\\[eval\\]', seconds: 120, profile, onLine: () => {} });
      problems.push(...startProblems(visit, matched));
    }
  } finally {
    removeQuietly(profile);
  }
  return problems;
}

// [rc4l] Every problem with the site at base against the stage in dir, and the line that sums it up.
export async function verifySite({ base, dir, browser = true, fetch = globalThis.fetch, run = smoke }) {
  const files = stagedFiles(dir);
  const problems = await redirectProblems(base, fetch);
  for (const f of files) problems.push(...await fileProblems(base, f, fetch));
  if (browser) problems.push(...await browserProblems(base, run));
  const summary = problems.length ? `${base}: ${problems.length} problems` : `${base}: the redirect, every staged file (${files.length}) and its revisit${browser ? ', and two browser visits,' : ''} are as staged`;
  return { problems, summary };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const config = readConfig();
  const base = (args.find((a) => !a.startsWith('--')) ?? siteUrl(config)).replace(/\/$/, '');
  const { problems, summary } = await verifySite({ base, dir: siteDir(config), browser: !args.includes('--no-browser') });
  for (const p of problems) console.log(`FAIL ${p}`);
  console.log(summary);
  process.exit(problems.length ? 1 : 0);
}
