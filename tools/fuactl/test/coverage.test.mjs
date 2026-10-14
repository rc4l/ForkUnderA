import { test } from "node:test";
import assert from "node:assert/strict";
import { gatedUnits, parseCobertura, judgeCoverage, unitsTouched, findOpenCppCoverage, isJumpLine } from "../src/coverage.mjs";

test("a test gates the sibling source and header that exist", () => {
  const exists = (p) => ["a/x.cpp", "a/x.h", "b/y.h"].includes(p);
  assert.deepEqual(gatedUnits(["a/x_test.cpp", "b/y_test.cpp", "c/z_test.cpp"], exists), ["a/x.cpp", "a/x.h", "b/y.h"]);
});

const REPORT = `<coverage><packages><package><classes>
<class name="x.cpp" filename="ForkUnderA\\src\\a\\x.cpp" line-rate="0.5"><lines>
<line number="3" hits="1"/><line number="4" hits="0"/></lines></class>
<class name="y.h" filename="ForkUnderA\\src\\b\\y.h" line-rate="1"><lines>
<line number="7" hits="2"/></lines></class>
</classes></package></packages></coverage>`;

test("a Cobertura report gives each file's covered, total and missed lines", () => {
  assert.deepEqual(parseCobertura(REPORT), {
    "ForkUnderA/src/a/x.cpp": { hit: 1, total: 2, missed: [4] },
    "ForkUnderA/src/b/y.h": { hit: 1, total: 1, missed: [] },
  });
});

test("a unit under 100% fails, and one the report never saw passes only as a header", () => {
  const files = parseCobertura(REPORT);
  const r = judgeCoverage(["src/a/x.cpp", "src/b/y.h", "src/c/z.h", "src/c/z.cpp"], files);
  assert.deepEqual(r.map((x) => [x.unit, x.ok]), [["src/a/x.cpp", false], ["src/b/y.h", true], ["src/c/z.h", true], ["src/c/z.cpp", false]]);
  assert.deepEqual(r[0].missed, [4]);
  assert.equal(r[2].unseen, true);
});

test("a missed loop brace or continue is MSVC's jump, not an unrun line", () => {
  assert.equal(isJumpLine("\t}"), true);
  assert.equal(isJumpLine("  continue;"), true);
  assert.equal(isJumpLine("\treturn false;"), false);
  const files = parseCobertura(REPORT);
  const brace = judgeCoverage(["src/a/x.cpp"], files, (unit, n) => (n === 4 ? "\t}" : "x = 1;"));
  assert.deepEqual([brace[0].ok, brace[0].missed, brace[0].hit], [true, [], 2]);
});

test("a change touches a unit through its source, header or test", () => {
  const units = ["a/x.cpp", "a/x.h", "b/y.cpp", "c/z.cpp"];
  assert.deepEqual(unitsTouched(units, ["a\\x.h"]), ["a/x.cpp", "a/x.h"]);
  assert.deepEqual(unitsTouched(units, ["b/y_test.cpp", "c/other.cpp"]), ["b/y.cpp"]);
});

test("OpenCppCoverage is found from the flag first, then the environment", () => {
  const exists = (p) => p === "D:/tool.exe" || p === "E:/env.exe";
  assert.equal(findOpenCppCoverage("D:/tool.exe", { FUACTL_OPENCPPCOVERAGE: "E:/env.exe" }, exists), "D:/tool.exe");
  assert.equal(findOpenCppCoverage(undefined, { FUACTL_OPENCPPCOVERAGE: "E:/env.exe" }, exists), "E:/env.exe");
});
