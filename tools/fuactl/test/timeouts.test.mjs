import { test } from "node:test";
import assert from "node:assert/strict";
import { scaled, timeoutScale } from "../src/timeouts.mjs";

test("timeouts stretch by FUACTL_TIMEOUT_SCALE and never shrink", () => {
  assert.equal(timeoutScale({}), 1);
  assert.equal(timeoutScale({ FUACTL_TIMEOUT_SCALE: "20" }), 20);
  assert.equal(timeoutScale({ FUACTL_TIMEOUT_SCALE: "0.5" }), 1);
  assert.equal(timeoutScale({ FUACTL_TIMEOUT_SCALE: "fast" }), 1);
  assert.equal(scaled(8000, { FUACTL_TIMEOUT_SCALE: "2.5" }), 20000);
  assert.equal(scaled(8000, {}), 8000);
});
