import { test } from "node:test";
import assert from "node:assert/strict";
import { goneBeforeBridge } from "../src/session.mjs";

test("an engine gone before its bridge is named by its exit code or the signal that killed it", () => {
  assert.equal(goneBeforeBridge({ exitCode: null, signalCode: null }), null);
  assert.equal(goneBeforeBridge(null), null);
  assert.equal(goneBeforeBridge({ exitCode: null, signalCode: "SIGSEGV" }), "engine was killed by SIGSEGV before bridge came up");
  assert.match(goneBeforeBridge({ exitCode: 1, signalCode: null }), /^engine exited \(1\) before bridge came up$/);
});
