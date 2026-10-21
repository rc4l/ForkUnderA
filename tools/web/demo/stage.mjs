#!/usr/bin/env node
// [rc4l] Stages a built page (dist-web) as the demo site's assets: the page under demo/, each file over Workers' asset limit stored gzipped as <name>.gz, which the Worker sends as stored.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { DEMO_DIR, readConfig, siteDir } from './config.mjs';

// [rc4l] Workers' static assets take no file over 25 MiB.
export const ASSET_LIMIT = 25 * 1024 * 1024;

// [rc4l] Replaces outDir's demo/, so outDir must be empty or hold only a previous stage; returns the files stored gzipped, relative to the page.
export function stageDemo({ distDir, outDir, limit = ASSET_LIMIT }) {
  if (!fs.existsSync(path.join(distDir, 'index.html'))) throw new Error(`no ${path.join(distDir, 'index.html')}: stage the web build first`);
  if (fs.existsSync(outDir) && fs.readdirSync(outDir).some((e) => e !== 'demo')) throw new Error(`${outDir} holds more than a demo stage; refusing to empty it`);
  const page = path.join(outDir, 'demo');
  fs.rmSync(page, { recursive: true, force: true });
  fs.cpSync(distDir, page, { recursive: true });
  const packed = [];
  for (const rel of fs.readdirSync(page, { recursive: true })) {
    const f = path.join(page, rel);
    if (!fs.statSync(f).isFile() || fs.statSync(f).size <= limit) continue;
    const gz = zlib.gzipSync(fs.readFileSync(f), { level: 9 });
    if (gz.length > limit) throw new Error(`${f} is over the asset limit even gzipped (${gz.length} bytes)`);
    fs.writeFileSync(f + '.gz', gz);
    fs.rmSync(f);
    packed.push(rel.split(path.sep).join('/'));
  }
  return packed.sort();
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const distDir = path.resolve(process.argv[2] ?? path.join(DEMO_DIR, '..', '..', '..', 'dist-web'));
  const outDir = path.resolve(process.argv[3] ?? siteDir(readConfig()));
  const packed = stageDemo({ distDir, outDir });
  console.log(`staged ${distDir} into ${outDir}/demo${packed.length ? `; gzipped ${packed.join(', ')}` : ''}`);
}
