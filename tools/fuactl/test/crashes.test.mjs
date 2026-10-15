import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { recentCrashes, summarizeCrash } from "../src/crashes.mjs";

const CRASH = `=== MCP ENGINE CRASH ===
code: 0xc0000005
fault_addr: 00007FF7C5946912
pid: 31504

backtrace:
fault stack:
  #0 FConsoleBuffer::AddText + 0x32  c_consolebuffer.cpp:102
  #1 AddToConsole + 0x171  c_console.cpp:766
  #2 PrintString + 0x2fb  c_console.cpp:908`;

const ABORT = `=== MCP ENGINE ABORT ===
reason: vectored: exception 0xc0000005 at 00007FFBB619DABF
pid: 47724

backtrace:
  #0 WriteAbortReport + 0x94  [0x00007FF666E97534]
  #1 MCP_VectoredFilter + 0x7c  [0x00007FF666E9742C]`;

test("a crash file's gist is its code and the top of its fault stack", () => {
  assert.deepEqual(summarizeCrash(CRASH, 2), { why: "code: 0xc0000005", stack: ["#0 FConsoleBuffer::AddText + 0x32  c_consolebuffer.cpp:102", "#1 AddToConsole + 0x171  c_console.cpp:766"] });
});

test("an abort is summed up by its reason and its backtrace", () => {
  assert.deepEqual(summarizeCrash(ABORT), { why: "reason: vectored: exception 0xc0000005 at 00007FFBB619DABF", stack: ["#0 WriteAbortReport + 0x94  [0x00007FF666E97534]", "#1 MCP_VectoredFilter + 0x7c  [0x00007FF666E9742C]"] });
  assert.deepEqual(summarizeCrash("nothing"), { why: "", stack: [] });
});

test("recent crash files are found in fuactl's folders, newest first, and older ones left out", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "crashes-"));
  for (const [sub, name, ageMs] of [["fuactl-a", "engine-7799.log.crash", 1000], ["fuactl-b", "engine-7801.log.crash", 0], ["fuactl-c", "engine-7796.log.crash", 10 * 3600 * 1000], ["other", "x.crash", 0]]) {
    fs.mkdirSync(path.join(dir, sub), { recursive: true });
    const file = path.join(dir, sub, name);
    fs.writeFileSync(file, CRASH);
    const at = (Date.now() - ageMs) / 1000;
    fs.utimesSync(file, at, at);
  }
  fs.writeFileSync(path.join(dir, "fuactl-file"), "not a folder");
  const found = recentCrashes(Date.now() - 3600 * 1000, dir).map((c) => path.basename(path.dirname(c.file)));
  assert.deepEqual(found, ["fuactl-b", "fuactl-a"]);
});
