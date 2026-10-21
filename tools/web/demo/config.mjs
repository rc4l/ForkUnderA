// [rc4l] The demo site's wrangler.jsonc, read so the stage and its check take the folder and address the deploy uses from the one place the deploy reads them.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const DEMO_DIR = path.dirname(fileURLToPath(import.meta.url));

// [rc4l] A JSONC file's object, where each comment must be a line of its own.
export function parseJsonc(text) {
  return JSON.parse(text.split('\n').filter((l) => !l.trim().startsWith('//')).join('\n'));
}

export function readConfig(file = path.join(DEMO_DIR, 'wrangler.jsonc')) {
  return parseJsonc(fs.readFileSync(file, 'utf8'));
}

// [rc4l] The folder the deploy uploads, which the config names relative to itself.
export function siteDir(config, dir = DEMO_DIR) {
  return path.resolve(dir, config.assets.directory);
}

// [rc4l] The address the deploy serves at, its first custom domain.
export function siteUrl(config) {
  const route = (config.routes ?? []).find((r) => r.custom_domain);
  if (!route) throw new Error('wrangler.jsonc names no custom domain to serve at');
  return `https://${route.pattern}`;
}
