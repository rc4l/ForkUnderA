import { test } from "node:test";
import assert from "node:assert/strict";
import { validationMode, validationLaunch, parseValidationMessages, noteValidation, takeValidationReports, describeValidation, missingLayer } from "../src/validation.mjs";

// [Doruk-Aksoy] Two layer messages as the engine console prints them, with a plain Diligent warning that is no layer message between them.
const LOG = [
  "[Diligent ERROR] Vulkan debug message (validation): SYNC-HAZARD-WRITE-AFTER-READ",
  "                 vkQueueSubmit(): pSubmits[0] WRITE_AFTER_READ hazard detected.",
  "[Diligent warn] Pipeline 'x' is not ready",
  "[Diligent ERROR] Vulkan debug message (validation): SYNC-HAZARD-WRITE-AFTER-READ",
  "[Diligent warn] Vulkan debug message (validation, performance): BestPractices-vkAllocateMemory-small-allocation",
].join("\n");

// [Doruk-Aksoy] The swapchain hazard Diligent makes on each image's first use, beside a write-after-read on one of the engine's own images.
const SWAPCHAIN = [
  "[Diligent ERROR] Vulkan debug message (validation): SYNC-HAZARD-WRITE-AFTER-READ",
  "                 vkQueueSubmit(): pSubmits[0].pCommandBuffers[0] WRITE_AFTER_READ hazard detected. vkCmdPipelineBarrier performs image layout transition on the VkImage 0xd, which was previously accessed by vkAcquireNextImageKHR.",
  "[Diligent ERROR] Vulkan debug message (validation): SYNC-HAZARD-WRITE-AFTER-READ",
  "                 vkCmdDraw(): WRITE_AFTER_READ hazard detected. vkCmdDraw writes to VkImage 0xe, which was previously read by vkCmdDraw.",
].join("\n");

test("a known message is left out by its text, and the same id with other text still counts", () => {
  assert.deepEqual(parseValidationMessages(SWAPCHAIN), [{ severity: "error", kind: "validation", id: "SYNC-HAZARD-WRITE-AFTER-READ", count: 1 }]);
  assert.deepEqual(parseValidationMessages(SWAPCHAIN, []), [{ severity: "error", kind: "validation", id: "SYNC-HAZARD-WRITE-AFTER-READ", count: 2 }]);
});

test("FUACTL_VALIDATE picks the mode: unset or 0 is off, sync and gpu add their checks, anything else is the core checks", () => {
  assert.equal(validationMode({}), "");
  assert.equal(validationMode({ FUACTL_VALIDATE: "0" }), "");
  assert.equal(validationMode({ FUACTL_VALIDATE: " Sync " }), "sync");
  assert.equal(validationMode({ FUACTL_VALIDATE: "gpu" }), "gpu");
  assert.equal(validationMode({ FUACTL_VALIDATE: "1" }), "core");
  assert.equal(validationMode({ FUACTL_VALIDATE: "core" }), "core");
});

test("a validated launch sets fua_dg_validation from the command line, sync asks the layer for its synchronization checks and gpu for its shader checks", () => {
  assert.deepEqual(validationLaunch(""), { args: [], env: {} });
  assert.deepEqual(validationLaunch("core"), { args: ["+set", "fua_dg_validation", "1"], env: {} });
  assert.deepEqual(validationLaunch("sync"), { args: ["+set", "fua_dg_validation", "1"], env: { VK_LAYER_VALIDATE_SYNC: "1" } });
  assert.deepEqual(validationLaunch("gpu"), { args: ["+set", "fua_dg_validation", "1"], env: { VK_LAYER_GPUAV_ENABLE: "1" } });
});

test("the layer's messages are counted by severity and id, most frequent first, and other Diligent lines are not among them", () => {
  assert.deepEqual(parseValidationMessages(LOG), [
    { severity: "error", kind: "validation", id: "SYNC-HAZARD-WRITE-AFTER-READ", count: 2 },
    { severity: "warning", kind: "validation, performance", id: "BestPractices-vkAllocateMemory-small-allocation", count: 1 },
  ]);
  assert.deepEqual(parseValidationMessages("nothing here"), []);
});

test("a stopped validated instance's messages are kept until taken, and an instance without validation, a log or messages keeps nothing", () => {
  takeValidationReports();
  noteValidation({ validate: "core", log: "a.log" }, () => LOG);
  noteValidation({ validate: "", log: "b.log" }, () => LOG);
  noteValidation({ validate: "core" }, () => LOG);
  noteValidation({ validate: "core", log: "c.log" }, () => "clean");
  noteValidation({ validate: "core", log: "d.log" }, () => { throw new Error("gone"); });
  noteValidation(null);
  const taken = takeValidationReports();
  assert.equal(taken.length, 1);
  assert.equal(taken[0].log, "a.log");
  assert.deepEqual(takeValidationReports(), []);
});

test("a run's reports read as one failing line, errors first and each id summed over launches; none reads as nothing", () => {
  const one = { log: "a.log", messages: parseValidationMessages(LOG) };
  const two = { log: "b.log", messages: [{ severity: "warning", kind: "validation", id: "Z-WARN", count: 4 }, { severity: "error", kind: "validation", id: "SYNC-HAZARD-WRITE-AFTER-READ", count: 1 }] };
  assert.deepEqual(describeValidation([one, two]), [
    "FAIL validation: 8 message(s): SYNC-HAZARD-WRITE-AFTER-READ error x3, Z-WARN warning x4, BestPractices-vkAllocateMemory-small-allocation warning x1 (a.log, b.log)",
  ]);
  assert.deepEqual(describeValidation([]), []);
});

test("a run with no validation layer fails rather than passing unchecked", () => {
  assert.deepEqual(missingLayer("[Diligent warn] Validation layer VK_LAYER_KHRONOS_validation is not available.\n").map((m) => m.id), ["VALIDATION-LAYER-NOT-INSTALLED"]);
  assert.deepEqual(missingLayer("vulkan: uploaded\n"), []);
});
