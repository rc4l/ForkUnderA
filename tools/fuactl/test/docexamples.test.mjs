import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { parseStep, docExamples, allDocExamples, exampleFaults, bccPaths, compileBcs } from "../src/docexamples.mjs";
import { buildDocsPk3, runExample, judgeWith, compareDocs, TIDS } from "../scenarios/acsdocs.mjs";

const PAGE = ["intro", "```bcs", '#library "fuaone"', "script 1 ( void ) {}", "```", "", "<!-- example", "pukename A 1 => /x;y/i", "puke 2", "@changemap MAP01", "-->",
  "```bcs", '#library "fuatwo"', "```", "<!-- example: compile -->", "```bcs", "no library", "```"].join("\r\n");

test("a page's bcs blocks come with their library, line and steps, a marker line a step each", () => {
  const [one, two, three] = docExamples(PAGE, "p.md");
  assert.deepEqual([one.library, one.line, one.compileOnly], ["fuaone", 2, false]);
  assert.equal(one.code, '#library "fuaone"\nscript 1 ( void ) {}\n');
  assert.deepEqual(one.steps.map((s) => [s.command, String(s.want)]), [["pukename A 1", "/x;y/i"], ["puke 2", "null"], ["@changemap MAP01", "null"]]);
  assert.deepEqual([two.library, two.steps, two.compileOnly], ["fuatwo", [], true]);
  assert.deepEqual([three.library, three.steps], [null, null]);
  assert.deepEqual(parseStep("  say hi  "), { command: "say hi", want: null });
});

test("faults: no marker, no library, a name too long or taken twice", () => {
  const examples = docExamples(PAGE, "p.md");
  assert.deepEqual(exampleFaults(examples), ["p.md:16: no <!-- example: ... --> marker after the block", "p.md:16: no #library line"]);
  assert.deepEqual(exampleFaults([...examples.slice(0, 2), { ...examples[0], line: 9 }, { ...examples[0], library: "toolongname", line: 3 }]),
    ["p.md:9: library \"fuaone\" is also p.md:2's", "p.md:3: library \"toolongname\" is longer than 8 characters"]);
});

test("every example in docs/modding/acs has a marker, a library of its own and a name zt-bcc takes", () => {
  const examples = allDocExamples();
  assert.ok(examples.length > 0);
  assert.deepEqual(exampleFaults(examples), []);
});

test("zt-bcc is FUACTL_BCC, else the checkout beside the repository, and a compile passes the target and fails on a warning", () => {
  assert.equal(bccPaths({ FUACTL_BCC: "X:/b/build/zt-bcc.exe" }).lib, path.resolve("X:/b/lib"));
  assert.equal(bccPaths({ FUACTL_BCC: "X:/b/zt.exe", FUACTL_BCC_LIB: "L" }).lib, "L");
  assert.match(bccPaths({}).exe, /zt-bcc[\\/]build[\\/]zt-bcc\.exe$/);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "dex-test-"));
  const calls = [];
  const ok = (exe, args) => { calls.push(args); fs.writeFileSync(args.at(-1), "OBJ"); return { status: 0, stdout: "" }; };
  assert.equal(compileBcs("code", "fuaone", dir, { exe: "b", lib: "l" }, ok).toString(), "OBJ");
  assert.deepEqual(calls[0].slice(0, 4), ["-i", "l", "-target", "fua"]);
  assert.throws(() => compileBcs("code", "fuatwo", dir, { exe: "b", lib: "l" }, () => ({ status: 0, stdout: "x.bcs:1: warning: odd" })), /doesn't compile cleanly[\s\S]*warning/);
  assert.throws(() => compileBcs("code", "fuatwo", dir, { exe: "b", lib: "l" }, () => ({ status: 1, stderr: "boom" })), /boom/);
});

test("the pk3 holds the map, the driver and every example as libraries LOADACS loads", () => {
  const compiled = [];
  const pk3 = buildDocsPk3(docExamples(PAGE).slice(0, 2), (code, name) => { compiled.push(name); return Buffer.from(name); }).toString("latin1");
  assert.deepEqual(compiled, ["fuadrv", "fuaone", "fuatwo"]);
  for (const name of ["maps/MAP01.wad", "acs/fuadrv.o", "acs/fuaone.o", "LOADACS.txt", "fuadrv\nfuaone\nfuatwo\n", "decorate.txt", "user_kills"]) assert.ok(pk3.includes(name), name);
  assert.deepEqual(TIDS, { barrels: [11, 12, 13], imps: [31, 32, 33], target: 21 });
});

test("an example's steps run in order, a changemap waiting for the level, and each miss names the page, line and output", async () => {
  const [one] = docExamples(PAGE, "p.md");
  const ran = [];
  const faults = await runExample(one, async (t) => { ran.push(t); return t === "pukename A 1" ? "said X;Y" : ""; }, async (t) => { ran.push(`enter ${t}`); return ""; });
  assert.deepEqual(ran, ["pukename A 1", "puke 2", "enter changemap MAP01"]);
  assert.deepEqual(faults, []);
  const missed = await runExample(one, async () => "other", async () => "");
  assert.deepEqual(missed, ["p.md:2 pukename A 1: other (want /x;y/i)"]);
});

test("a missing zt-bcc skips, a page's faults fail before any launch, and otherwise the examples run", async () => {
  assert.match((await compareDocs({}, { examples: [], paths: { exe: "Z:/none/zt-bcc.exe" } })).lines[0], /^SKIP acsdocs: no zt-bcc at/);
  const exe = fs.mkdtempSync(path.join(os.tmpdir(), "dex-bcc-")) + "/zt-bcc.exe";
  fs.writeFileSync(exe, "");
  const bad = await compareDocs({}, { examples: docExamples(PAGE, "p.md"), paths: { exe, lib: "l" } });
  assert.equal(bad.failed, 2);
  assert.equal(typeof judgeWith([]), "function");
});
