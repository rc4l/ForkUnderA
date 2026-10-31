#!/usr/bin/env node
// [rc4l] Assembles dist-web/ from a web build: the module, the page, and the data the page writes into the engine's file system, with its manifest.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// [rc4l] The loading screen's logo, the app icon's one copy rather than a second in web/.
const LOGO = fileURLToPath(new URL('../../src/zandronum/wadsrc/static/fua_appicon.png', import.meta.url));

// [rc4l] The page's Source link as index.html writes it, which a stage points at the commit it was built from.
const SOURCE = 'https://github.com/rc4l/ForkUnderA/tree/';

// [rc4l] The page with its Source link on commit, so a player reaches the code of the build they are playing.
export function pinSource(html, commit) {
  return html.replace(SOURCE + 'main', SOURCE + commit);
}

// [rc4l] The commit checked out at root, or null outside a git checkout, which leaves the link on main.
export function headCommit(root) {
  try { return execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim(); }
  catch { return null; }
}

// [rc4l] The engine's own data in a build directory: every pk3 its wadsrc targets wrote there.
export function engineData(buildDir) {
  return fs.readdirSync(buildDir).filter((f) => f.toLowerCase().endsWith('.pk3')).sort().map((f) => path.join(buildDir, f));
}

// [rc4l] files.json: each data file's name and size, which the page fetches in order and writes at the file system's root.
export function manifest(files) {
  return files.map((f) => ({ name: path.basename(f), size: fs.statSync(f).size }));
}

// [rc4l] wads are more files for the page to write in, such as an IWAD and a map the query then names (?iwad=doom2.wad&file=map.wad).
export function stage({ buildDir, outDir, pageDir, freedoomDir, module = 'forkundera', wads = [], commit = null }) {
  const data = path.join(outDir, 'data');
  fs.mkdirSync(data, { recursive: true });
  for (const ext of ['.js', '.wasm']) {
    const src = path.join(buildDir, module + ext);
    if (!fs.existsSync(src)) throw new Error(`no ${src}: build first`);
    fs.copyFileSync(src, path.join(outDir, module + ext));
  }
  for (const f of fs.readdirSync(pageDir)) fs.copyFileSync(path.join(pageDir, f), path.join(outDir, f));
  fs.copyFileSync(LOGO, path.join(outDir, 'logo.png'));
  const page = path.join(outDir, 'index.html');
  if (commit) fs.writeFileSync(page, pinSource(fs.readFileSync(page, 'utf8'), commit));
  const files = [...engineData(buildDir), path.join(freedoomDir, 'freedoom2.wad'), ...wads];
  for (const f of files) fs.copyFileSync(f, path.join(data, path.basename(f)));
  fs.copyFileSync(path.join(freedoomDir, 'License.txt'), path.join(data, 'freedoom-license.txt'));
  fs.writeFileSync(path.join(data, 'files.json'), JSON.stringify(manifest(files), null, 1));
  return files.map((f) => path.basename(f));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
  const args = process.argv.slice(2);
  const at = args.indexOf('--wads');
  const wads = at >= 0 ? args.splice(at, 2)[1].split(',').map((w) => path.resolve(w)) : [];
  const buildDir = path.resolve(args[0] ?? path.join(root, 'build-web'));
  const outDir = path.resolve(args[1] ?? path.join(root, 'dist-web'));
  const staged = stage({ buildDir, outDir, pageDir: path.join(root, 'web'), freedoomDir: path.join(root, 'tools', 'freedoom'), wads, commit: headCommit(root) });
  console.log(`staged ${outDir}: ${staged.join(', ')}`);
}
