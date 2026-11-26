import { test } from "node:test";
import assert from "node:assert/strict";
import { parseEnginePids, engineCheck } from "../src/registry.mjs";

test("engine pids are read from tasklist's CSV and from ps, and nothing else counts", () => {
  const tasklist = '"forkundera.exe","7712","Console","1","120,000 K"\r\n"forkundera.exe","26592","Console","1","98,000 K"\r\n';
  assert.deepEqual([...parseEnginePids(tasklist)], [7712, 26592]);
  assert.deepEqual([...parseEnginePids("INFO: No tasks are running which match the specified criteria.")], []);
  const ps = "  101 /usr/local/bin/forkundera\n  102 node\n  103 forkundera\n  104 forkundera-server-registry\n";
  assert.deepEqual([...parseEnginePids(ps)], [101, 103]);
  assert.deepEqual([...parseEnginePids("")], []);
});

// [Doruk-Aksoy] One listing per check, so a pid another program took since the entry was written is not taken for an engine.
test("engineCheck lists the engines once and answers from that listing on either platform", () => {
  const calls = [];
  const run = (cmd, args) => { calls.push([cmd, args[0]]); return { stdout: cmd === "tasklist" ? '"forkundera.exe","42","Console","1","1 K"' : "  42 forkundera\n" }; };
  const win = engineCheck({ run, platform: "win32" });
  assert.deepEqual([win(42), win(43)], [true, false]);
  const linux = engineCheck({ run, platform: "linux" });
  assert.deepEqual([linux(42), linux(7)], [true, false]);
  assert.deepEqual(calls, [["tasklist", "/FI"], ["ps", "-A"]]);
  assert.equal(engineCheck({ run: () => null, platform: "win32" })(42), false);
});
