// [rc4l] The headers a page from the web build is served with, shared by everything that serves it, so none imports another's runtime.

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
  const ext = /\.[^./\\]*$/.exec(file)?.[0].toLowerCase();
  return TYPES[ext] ?? 'application/octet-stream';
}
