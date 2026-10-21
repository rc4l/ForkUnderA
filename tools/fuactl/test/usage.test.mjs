import { test } from "node:test";
import assert from "node:assert/strict";
import { usageFor } from "../src/usage.mjs";

const USAGE = "fuactl <command>\n  play [--map M]   a hands-on instance\n  ls   list instances\n  playback F   replay";

// [rc4l] `play --help` launched an unlocked engine on a free port instead of saying what play takes.
test("a command's help is its own usage lines, and an unknown or missing command gets the whole text", () => {
  assert.equal(usageFor(USAGE, "play"), "  play [--map M]   a hands-on instance");
  assert.equal(usageFor(USAGE, "fuactl"), USAGE);   // [rc4l] the title line is not a command
  assert.equal(usageFor(USAGE, "nope"), USAGE);
  assert.equal(usageFor(USAGE, undefined), USAGE);
});
