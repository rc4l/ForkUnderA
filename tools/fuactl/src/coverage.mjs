// [rc4l] The 100% line-coverage gate on Windows, which tests/coverage.sh --auto runs with llvm-cov elsewhere: each NAME_test.cpp gates its NAME.cpp and NAME.h, measured by OpenCppCoverage over the unit tests.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { pickCmake, compile } from "./shot.mjs";

// [rc4l] The units the tests gate: each test file's sibling .cpp and .h that exist.
export function gatedUnits(testFiles, exists) {
  const units = [];
  for (const t of testFiles) {
    const base = t.slice(0, -"_test.cpp".length);
    for (const ext of [".cpp", ".h"]) if (exists(base + ext)) units.push(base + ext);
  }
  return units;
}

// [rc4l] Each file's lines from a Cobertura report, keyed by its path with forward slashes: covered, total and the missed line numbers.
export function parseCobertura(xml) {
  const files = {};
  for (const m of xml.matchAll(/<class\b[^>]*\bfilename="([^"]+)"[^>]*>([\s\S]*?)<\/class>/g)) {
    const name = m[1].replace(/\\/g, "/");
    const f = files[name] || (files[name] = { hit: 0, total: 0, missed: [] });
    for (const l of m[2].matchAll(/<line\b[^>]*\bnumber="(\d+)"[^>]*\bhits="(\d+)"/g)) {
      f.total++;
      if (Number(l[2]) > 0) f.hit++;
      else f.missed.push(Number(l[1]));
    }
  }
  return files;
}

// [rc4l] Whether a missed line is only a loop's closing brace or a `continue`, where MSVC puts the jump that llvm-cov counts with the line before it.
export function isJumpLine(text) {
  const t = (text || "").trim();
  return t === "}" || t === "continue;";
}

// [rc4l] Each unit (a path relative to the repository) against the report, `lineOf(unit, n)` giving its source lines: under 100% fails with its missed lines, and one the report never saw passes only as a header, which may hold no code.
export function judgeCoverage(units, files, lineOf = () => "") {
  const keys = Object.keys(files);
  return units.map((unit) => {
    const key = keys.find((k) => k.toLowerCase().endsWith(unit.toLowerCase()));
    if (!key) return { unit, ok: unit.endsWith(".h"), unseen: true };
    const f = files[key];
    const missed = f.missed.filter((n) => !isJumpLine(lineOf(unit, n)));
    return { unit, ok: missed.length === 0, hit: f.total - missed.length, total: f.total, missed };
  });
}

// [rc4l] The units a change touched: one whose source, header or test is among `changed` (paths relative to the repository).
export function unitsTouched(units, changed) {
  const set = new Set(changed.map((c) => c.replace(/\\/g, "/")));
  return units.filter((u) => {
    const base = u.replace(/\.(cpp|h)$/, "");
    return set.has(u) || set.has(`${base}.cpp`) || set.has(`${base}.h`) || set.has(`${base}_test.cpp`);
  });
}

// [rc4l] OpenCppCoverage: --tool, FUACTL_OPENCPPCOVERAGE, its usual install folder, or PATH; null when none is there.
export function findOpenCppCoverage(tool, env = process.env, exists = fs.existsSync) {
  const candidates = [tool, env.FUACTL_OPENCPPCOVERAGE, "C:/Program Files/OpenCppCoverage/OpenCppCoverage.exe"].filter((c) => c && c !== true);
  for (const c of candidates) if (exists(c)) return c;
  const where = spawnSync("where", ["OpenCppCoverage.exe"], { encoding: "utf8" });
  const found = (where.stdout || "").split(/\r?\n/).find((l) => l.trim());
  return found ? found.trim() : null;
}

function listTests(dir, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) listTests(p, out);
    else if (e.name.endsWith("_test.cpp")) out.push(p);
  }
  return out;
}

// [rc4l] Run the unit tests under OpenCppCoverage and judge the gated units, all of them or those changed since `since`; returns the results and the report's path.
export function runCoverage({ root, tool, since, log = console.error }) {
  const exe = findOpenCppCoverage(tool);
  if (!exe) throw new Error("OpenCppCoverage not found: pass --tool PATH or set FUACTL_OPENCPPCOVERAGE");
  // [rc4l] The Debug tests, since Release has no symbols to read lines from, built here so the report is never of a stale binary.
  compile(pickCmake(), root, "build-tests", "zandrox_tests", log, "Debug");
  const tests = path.join(root, "build-tests", "Debug", "zandrox_tests.exe");
  const src = path.join(root, "src", "zandronum", "src");
  let units = gatedUnits(listTests(src), fs.existsSync).map((u) => path.relative(root, u).replace(/\\/g, "/"));
  if (since) {
    const diff = spawnSync("git", ["-C", root, "diff", "--name-only", since], { encoding: "utf8" });
    if (diff.status !== 0) throw new Error(`git diff ${since} failed: ${diff.stderr}`);
    // [rc4l] A new file is untracked until it is added, and a gate that skipped it would pass the unit most likely to lack tests.
    const fresh = spawnSync("git", ["-C", root, "ls-files", "--others", "--exclude-standard"], { encoding: "utf8" });
    units = unitsTouched(units, `${diff.stdout}\n${fresh.stdout || ""}`.split(/\r?\n/).filter(Boolean));
  }
  const report = path.join(root, "build-tests", "coverage.xml");
  log(`coverage: ${units.length} unit(s), report ${report}`);
  const run = spawnSync(exe, ["--quiet", "--sources", src, "--export_type", `cobertura:${report}`, "--", tests, "--gtest_brief=1"],
    { encoding: "utf8", maxBuffer: 1 << 26 });
  if (run.status !== 0) throw new Error(`the unit tests failed under coverage:\n${(run.stdout || "").slice(-2000)}${run.stderr || ""}`);
  const sources = {};
  const lineOf = (unit, n) => {
    if (!(unit in sources)) sources[unit] = fs.readFileSync(path.join(root, unit), "utf8").split(/\r?\n/);
    return sources[unit][n - 1];
  };
  return { results: judgeCoverage(units, parseCobertura(fs.readFileSync(report, "utf8")), lineOf), report };
}
