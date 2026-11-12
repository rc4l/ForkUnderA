import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { pngSize } from "../src/pngcore.mjs";

const GRAPHICS = fileURLToPath(new URL("../../../src/zandronum/wadsrc/static/graphics/", import.meta.url));

// [rc4l] I_SetCursor refuses a picture wider or taller than 32 and falls back to the OS pointer, so vid_cursor naming one would silently do nothing.
test("every game cursor the core pk3 ships fits I_SetCursor's 32 by 32", () => {
  const cursors = fs.readdirSync(GRAPHICS).filter((f) => /curs\.png$/i.test(f));
  assert.ok(cursors.length >= 5, `only ${cursors.length} cursors in ${GRAPHICS}`);
  for (const f of cursors) {
    const { width, height } = pngSize(fs.readFileSync(path.join(GRAPHICS, f)));
    assert.ok(width <= 32 && height <= 32, `${f} is ${width}x${height}`);
  }
});

test("a PNG's size comes from its header, and anything else has none", () => {
  const head = Buffer.alloc(24);
  Buffer.from("\x89PNG", "latin1").copy(head);
  head.writeUInt32BE(29, 16);
  head.writeUInt32BE(32, 20);
  assert.deepEqual(pngSize(head), { width: 29, height: 32 });
  assert.equal(pngSize(Buffer.from("GIF89a")), null);
});
