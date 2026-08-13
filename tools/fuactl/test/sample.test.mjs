import { test } from "node:test";
import assert from "node:assert/strict";
import { parseTopOfStack, parseLinuxPerf } from "../src/sample.mjs";

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
