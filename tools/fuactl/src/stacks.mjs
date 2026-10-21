// [rc4l] fuactl hang's view into a wedged engine on Windows: the busiest threads' call stacks from the stackdump helper (native/stackdump), where the in-engine sampler, which reports through the stuck game thread, cannot answer.
import { spawnSync } from "node:child_process";

// [rc4l] The helper's output as threads, busiest first, each with its frames as module!symbol lines.
export function parseStackdump(text) {
  const threads = [];
  for (const line of String(text).split(/\r?\n/)) {
    const head = /^=== thread (\d+), (\d+) ms CPU in 500 ms \(sample (\d+)\)$/.exec(line);
    if (head) { threads.push({ thread: Number(head[1]), cpuMs: Number(head[2]), sample: Number(head[3]), frames: [] }); continue; }
    if (threads.length && /^ {2}\S/.test(line)) threads[threads.length - 1].frames.push(line.trim());
  }
  return threads;
}

// [rc4l] The first frame of a stack in the named module, past the system and driver frames a thread waits in, as its symbol alone; null when the stack never reaches it.
export function moduleFrame(frames, module = "forkundera") {
  const f = frames.find((x) => x.startsWith(`${module}!`));
  return f ? f.slice(module.length + 1).split(/ {2}|\+0x/)[0] : null;
}

// [rc4l] The stacks of the process's `threads` busiest threads, from the helper at `exe`.
export function nativeStacks(pid, { exe, threads = 3, samples = 1, run = spawnSync } = {}) {
  const r = run(exe, [String(pid), String(threads), String(samples)], { encoding: "utf8" });
  if (r.status !== 0) return { available: false, error: (r.stderr || "").trim() || `exit ${r.status}` };
  return { available: true, threads: parseStackdump(r.stdout) };
}
