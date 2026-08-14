import { test } from "node:test";
import assert from "node:assert/strict";
import { parseTopOfStack, parseLinuxPerf, sampleProcess } from "../src/sample.mjs";

// A fake spawn: returns canned {err,stdout,stderr} per invocation, recording the commands.
function fakeRun(replies) {
  const calls = [];
  let i = 0;
  const run = async (cmd, args) => { calls.push({ cmd, args }); return replies[i++] || { err: null, stdout: "", stderr: "" }; };
  run.calls = calls;
  return run;
}

const MAC_OUT = [
  "Sort by top of stack, including counts of all functions:",
  "    FBlockThingsIterator::Next  (in forkundera)        412",
  "    R_DrawColumn  (in forkundera)        88",
  "",
].join("\n");

test("sampleProcess: macOS backend parses the sample output", async () => {
  const run = fakeRun([{ err: null, stdout: MAC_OUT, stderr: "" }]);
  const r = await sampleProcess(1234, { _platform: "darwin", _run: run, engineOnly: true, top: 5 });
  assert.equal(r.available, true);
  assert.equal(r.backend, "sample");
  assert.equal(r.functions[0].symbol, "FBlockThingsIterator::Next");
  assert.equal(run.calls[0].cmd, "sample");
});

test("sampleProcess: macOS with no output reports unavailable", async () => {
  const r = await sampleProcess(1, { _platform: "darwin", _run: fakeRun([{ err: { message: "boom" }, stdout: "", stderr: "" }]) });
  assert.equal(r.available, false);
  assert.match(r.error, /boom/);
});

test("sampleProcess: Linux backend records then reports", async () => {
  const perf = "  50.00%  forkundera  forkundera  [.] P_RunThinkers\n  10.00%  forkundera  forkundera  [.] R_DrawSpan\n";
  const run = fakeRun([{ err: null, stdout: "", stderr: "" }, { err: null, stdout: perf, stderr: "" }]);
  const r = await sampleProcess(9, { _platform: "linux", _run: run, engineOnly: true });
  assert.equal(r.available, true);
  assert.equal(r.backend, "perf");
  assert.equal(r.functions[0].symbol, "P_RunThinkers");
  assert.deepEqual(run.calls.map((c) => c.args[0]), ["record", "report"]);
});

test("sampleProcess: Linux surfaces perf-not-installed and perf-blocked", async () => {
  const notInstalled = await sampleProcess(9, { _platform: "linux", _run: fakeRun([{ err: { message: "spawn perf ENOENT" }, stdout: "", stderr: "" }]) });
  assert.equal(notInstalled.available, false);
  assert.match(notInstalled.error, /not installed/);

  const blocked = await sampleProcess(9, { _platform: "linux", _run: fakeRun([{ err: null, stdout: "", stderr: "perf_event_paranoid too high" }]) });
  assert.equal(blocked.available, false);
  assert.match(blocked.error, /perf_event_paranoid|blocked/);

  const noReport = await sampleProcess(9, { _platform: "linux", _run: fakeRun([{ err: null, stdout: "", stderr: "" }, { err: null, stdout: "", stderr: "" }]) });
  assert.equal(noReport.available, false);
  assert.match(noReport.error, /no output/);
});

test("sampleProcess: unsupported platform explains the fallback", async () => {
  const r = await sampleProcess(1, { _platform: "win32" });
  assert.equal(r.available, false);
  assert.equal(r.backend, "none");
  assert.match(r.error, /Tracy|Windows Performance Recorder/);
});

const SAMPLE = `
Analysis of sampling forkundera (pid 123) every 1 millisecond
Call graph:
    ... (tree omitted) ...

Sort by top of stack, same collapsed (when >= 5):
        mach_msg2_trap  (in libsystem_kernel.dylib)        4200
        P_MobjThinker  (in forkundera)        900
        A_Chase  (in forkundera)        640
        P_TryMove  (in forkundera)        410
        FRandom::operator()  (in forkundera)        120

Binary Images:
       0x100000000 forkundera
`;

test("parseTopOfStack extracts symbol/binary/samples, hottest first", () => {
  const fns = parseTopOfStack(SAMPLE, { top: 3 });
  assert.equal(fns.length, 3);
  assert.deepEqual(fns[0], { symbol: "mach_msg2_trap", binary: "libsystem_kernel.dylib", samples: 4200 });
  assert.equal(fns[1].symbol, "P_MobjThinker");
  assert.equal(fns[2].symbol, "A_Chase");
});

test("parseTopOfStack can filter to one binary (our engine's hot functions)", () => {
  const fns = parseTopOfStack(SAMPLE, { onlyBinary: "forkundera" });
  assert.deepEqual(fns.map((f) => f.symbol), ["P_MobjThinker", "A_Chase", "P_TryMove", "FRandom::operator()"]);
  assert.ok(fns.every((f) => f.binary === "forkundera"));
});

test("parseTopOfStack returns [] when the section is absent", () => {
  assert.deepEqual(parseTopOfStack("no such section here"), []);
});

const PERF = `# Samples: 10K
#
# Overhead  Command      Shared Object      Symbol
# ........  ...........  .................  ...................
#
    31.20%  forkundera   forkundera         [.] P_MobjThinker
    18.05%  forkundera   forkundera         [.] FBlockThingsIterator::Next
     9.10%  forkundera   libc.so.6          [.] __memmove_avx
     4.00%  forkundera   forkundera         [.] A_Chase
`;

test("parseLinuxPerf extracts symbol/dso/percent, hottest first", () => {
  const fns = parseLinuxPerf(PERF, { top: 3 });
  assert.equal(fns[0].symbol, "P_MobjThinker");
  assert.equal(fns[0].samples, 31.2);
  assert.equal(fns[1].symbol, "FBlockThingsIterator::Next");
  assert.equal(fns[2].binary, "libc.so.6");
});

test("parseLinuxPerf can filter to the engine binary", () => {
  const fns = parseLinuxPerf(PERF, { onlyBinary: "forkundera" });
  assert.deepEqual(fns.map((f) => f.symbol), ["P_MobjThinker", "FBlockThingsIterator::Next", "A_Chase"]);
});
