// [rc4l] Temporary folders for the tests: scratch() makes an empty one, and scratchFiles({ 'data/a.pk3': body }) one holding those files.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

export function scratch(prefix = 'fua-test-') {
  return fs.mkdtempSync(path.join(os.tmpdir(), prefix));
}

export function scratchFiles(files, prefix) {
  const dir = scratch(prefix);
  for (const [name, body] of Object.entries(files)) {
    fs.mkdirSync(path.dirname(path.join(dir, name)), { recursive: true });
    fs.writeFileSync(path.join(dir, name), body);
  }
  return dir;
}
