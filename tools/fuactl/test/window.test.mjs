import { test } from "node:test";
import assert from "node:assert/strict";
import { windowKind, parseWindowLines, shotScript } from "../src/window.mjs";

// [rc4l] Queue #41 was read off a hung GL window and a minimized backend one, with PowerShell written beside the tool each time.
test("a window is GL's or the backend's by its title", () => {
  assert.equal(windowKind("Fua v0.3.0  64-bit (experimental fac32fc2f27b)"), "gl");
  assert.equal(windowKind("ForkUnderA - Diligent (Vulkan) backend"), "vk");
  assert.equal(windowKind("Default IME"), null);
});

test("the listing's lines become windows with their state, and anything else is skipped", () => {
  const text = "noise\r\n290196720|118836|0|1|0,0,1040,679|Fua v0.3.0 | odd title\r\n2955214|118836|1|0|0,1364,160,1392|ForkUnderA - Diligent (Vulkan) backend\n\n";
  const w = parseWindowLines(text);
  assert.equal(w.length, 2);
  assert.deepEqual(w[0], { hwnd: "290196720", pid: 118836, minimized: false, hung: true,
    rect: { left: 0, top: 0, right: 1040, bottom: 679 }, title: "Fua v0.3.0 | odd title", kind: "gl" });
  assert.equal(w[1].minimized, true);
  assert.equal(w[1].kind, "vk");
});

test("a capture names the window and quotes the path PowerShell's way", () => {
  const s = shotScript("123", "C:/it's here/a.png");
  assert.match(s, /\[IntPtr\]123/);
  assert.match(s, /Save\('C:\/it''s here\/a\.png'\)/);
});
