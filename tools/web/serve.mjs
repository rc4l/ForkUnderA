#!/usr/bin/env node
// [rc4l] Serves a web build from localhost with wasm's MIME type and the cross-origin isolation its threads need.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.txt': 'text/plain; charset=utf-8',
};

// [rc4l] SharedArrayBuffer, which Emscripten's threads are built on, exists only in a cross-origin isolated page.
export const ISOLATION_HEADERS = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin',
};

export function contentType(file) {
  return TYPES[path.extname(file).toLowerCase()] ?? 'application/octet-stream';
}

// [rc4l] The file a request names under root, index.html for a directory, or null for anything that escapes root.
export function resolveInRoot(root, urlPath) {
  let rel;
  try { rel = decodeURIComponent(urlPath.split('?')[0]); } catch { return null; }
  if (rel.includes('\0')) return null;
  const base = path.resolve(root);
  const full = path.resolve(base, '.' + path.posix.normalize('/' + rel));
  if (full !== base && !full.startsWith(base + path.sep)) return null;
  return rel.endsWith('/') ? path.join(full, 'index.html') : full;
}

export function createServer(root) {
  return http.createServer((req, res) => {
    let file = resolveInRoot(root, req.url ?? '/');
    if (file && fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, 'index.html');
    if (!file || !fs.existsSync(file)) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8', ...ISOLATION_HEADERS });
      res.end('not found\n');
      return;
    }
    res.writeHead(200, { 'Content-Type': contentType(file), 'Cache-Control': 'no-cache', ...ISOLATION_HEADERS });
    fs.createReadStream(file).pipe(res);
  });
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = process.argv[2] ?? '.';
  const port = Number(process.argv[3] ?? 8080);
  createServer(root).listen(port, '127.0.0.1', () => console.log(`serving ${path.resolve(root)} at http://localhost:${port}/`));
}
