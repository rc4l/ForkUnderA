import { test } from "node:test";
import assert from "node:assert/strict";
import { parseStackdump, moduleFrame, nativeStacks } from "../src/stacks.mjs";

const DUMP = [
  "=== thread 30960, 203 ms CPU in 500 ms (sample 1)",
  "  win32u!NtDxgkSubmitPresentToHwQueue+0x14",
  "  forkundera!dawn::native::d3d::SwapChain::PresentDXGISwapChain+0x66",
  "  forkundera!zx::hwrender::PresentFrame  F:\\src\\dgscene.cpp:711",
  "=== thread 17856, 156 ms CPU in 500 ms (sample 1)",
  "  ntdll!ZwWaitForAlertByThreadId+0x14",
  "",
].join("\r\n");

test("the helper's output becomes threads, busiest first, with their frames", () => {
  const threads = parseStackdump(DUMP);
  assert.deepEqual(threads.map((t) => [t.thread, t.cpuMs, t.sample, t.frames.length]), [[30960, 203, 1, 3], [17856, 156, 1, 1]]);
  assert.equal(threads[0].frames[2], "forkundera!zx::hwrender::PresentFrame  F:\\src\\dgscene.cpp:711");
  assert.deepEqual(parseStackdump("  stray frame before any thread\n"), []);
});

test("a stack's first frame in the engine is named without its module, offset or source line", () => {
  const [busiest, idle] = parseStackdump(DUMP);
  assert.equal(moduleFrame(busiest.frames), "dawn::native::d3d::SwapChain::PresentDXGISwapChain");
  assert.equal(moduleFrame(["forkundera!zx::hwrender::PresentFrame  F:\\src\\dgscene.cpp:711"]), "zx::hwrender::PresentFrame");
  assert.equal(moduleFrame(idle.frames), null);
});

test("the helper runs on the process and a failure says why", () => {
  let args = null;
  const ok = nativeStacks(42, { exe: "stackdump.exe", threads: 2, run: (exe, a) => { args = [exe, ...a]; return { status: 0, stdout: DUMP }; } });
  assert.deepEqual(args, ["stackdump.exe", "42", "2", "1"]);
  assert.equal(ok.available, true);
  assert.equal(ok.threads.length, 2);
  assert.deepEqual(nativeStacks(42, { exe: "x", run: () => ({ status: 1, stderr: "cannot open process 42\n" }) }), { available: false, error: "cannot open process 42" });
  assert.deepEqual(nativeStacks(42, { exe: "x", run: () => ({ status: 3 }) }), { available: false, error: "exit 3" });
});
