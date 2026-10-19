// [rc4l] The demo site's responses: the staged page from its assets with the isolation headers static hosts cannot send, and a file staged gzipped sent as it was stored.
import { ISOLATION_HEADERS, contentType } from '../headers.mjs';

// [rc4l] Where the page lives on the site, so the bare address can send a visitor there.
export const DEMO_PATH = '/demo/';

// [rc4l] A response with the isolation headers and any extra ones set; encodeBody 'manual' sends a body that is already compressed as it is.
export function isolated(res, extra = {}, encodeBody = 'automatic') {
  const headers = new Headers(res.headers);
  for (const [k, v] of Object.entries({ ...ISOLATION_HEADERS, ...extra })) headers.set(k, v);
  return new Response(res.body, { status: res.status, statusText: res.statusText, headers, encodeBody });
}

// [rc4l] One request against assets (the Worker's ASSETS binding): the site's root redirects to the page with its query, a file missing as named is tried as its stage's .gz.
export async function handle(request, assets) {
  const url = new URL(request.url);
  if (url.pathname === '/') return isolated(new Response(null, { status: 302, headers: { Location: DEMO_PATH + url.search } }));
  const res = await assets.fetch(request);
  if (res.status !== 404) return isolated(res);
  const gz = await assets.fetch(new Request(new URL(url.pathname + '.gz', url), request));
  // [rc4l] Only a .gz that is not there falls back to the 404, since a revisit's If-None-Match gets a 304 for one that is.
  if (gz.status === 404) return isolated(res);
  if (!gz.ok) return isolated(gz);
  return isolated(gz, { 'Content-Encoding': 'gzip', 'Content-Type': contentType(url.pathname) }, 'manual');
}
