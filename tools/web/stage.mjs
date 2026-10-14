#!/usr/bin/env node
// [rc4l] Assembles dist-web/ from a web build: the module, the page, and the data the page writes into the engine's file system, with its manifest.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// [rc4l] The engine's own data in a build directory: every pk3 its wadsrc targets wrote there.
export function engineData(buildDir) {
  return fs.readdirSync(buildDir).filter((f) => f.toLowerCase().endsWith('.pk3')).sort().map((f) => path.join(buildDir, f));
}

// [rc4l] files.json: each data file's name and size, which the page fetches in order and writes at the file system's root.
export function manifest(files) {
  return files.map((f) => ({ name: path.basename(f), size: fs.statSync(f).size }));
}

export function stage({ buildDir, outDir, pageDir, freedoomDir, module = 'forkundera' }) {
  const data = path.join(outDir, 'data');
  fs.mkdirSync(data, { recursive: true });
  for (const ext of ['.js', '.wasm']) {
    const src = path.join(buildDir, module + ext);
    if (!fs.existsSync(src)) throw new Error(`no ${src}: build first`);
    fs.copyFileSync(src, path.join(outDir, module + ext));
  }
  for (const f of fs.readdirSync(pageDir)) fs.copyFileSync(path.join(pageDir, f), path.join(outDir, f));
  const files = [...engineData(buildDir), path.join(freedoomDir, 'freedoom2.wad')];
  for (const f of files) fs.copyFileSync(f, path.join(data, path.basename(f)));
  fs.copyFileSync(path.join(freedoomDir, 'License.txt'), path.join(data, 'freedoom-license.txt'));
  fs.writeFileSync(path.join(data, 'files.json'), JSON.stringify(manifest(files), null, 1));
  return files.map((f) => path.basename(f));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
  const buildDir = path.resolve(process.argv[2] ?? path.join(root, 'build-web'));
  const outDir = path.resolve(process.argv[3] ?? path.join(root, 'dist-web'));
  const staged = stage({ buildDir, outDir, pageDir: path.join(root, 'web'), freedoomDir: path.join(root, 'tools', 'freedoom') });
  console.log(`staged ${outDir}: ${staged.join(', ')}`);
}
