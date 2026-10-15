import { test } from "node:test";
import assert from "node:assert/strict";
import { addedLines, commentIssues, markerFor, sentenceCount } from "../src/comments.mjs";

test("each kind of file has its comment marker, and other files none", () => {
  assert.equal(markerFor("src/a.cpp"), "//");
  assert.equal(markerFor("web/fua.js"), "//");
  assert.equal(markerFor("tools/x.sh"), "#");
  assert.equal(markerFor("src/CMakeLists.txt"), "#");
  assert.equal(markerFor("README.md"), null);
});

test("sentences are counted past abbreviations, file names and decimals", () => {
  assert.equal(sentenceCount(" One thing, e.g. Doom 2's blast in dgscene.cpp at 1.5 ms."), 1);
  assert.equal(sentenceCount(" One. Two."), 2);
  assert.equal(sentenceCount(" One. `two` follows."), 2);
  assert.equal(sentenceCount(""), 0);
});

test("a diff's added lines carry the new file's line numbers", () => {
  const diff = ["diff --git a/x.cpp b/x.cpp", "--- a/x.cpp", "+++ b/x.cpp", "@@ -3,2 +3,3 @@", " kept", "-gone", "+added\r", "+again", " kept"].join("\n");
  assert.deepEqual(addedLines(diff), [{ file: "x.cpp", line: 4, text: "added" }, { file: "x.cpp", line: 5, text: "again" }]);
});

test("a [rc4l] comment of two sentences, or one carried onto a second line, is an issue", () => {
  const added = [
    { file: "a.cpp", line: 1, text: "\t// [rc4l] Fine as one sentence." },
    { file: "a.cpp", line: 2, text: "\t// [rc4l] First. Second." },
    { file: "a.cpp", line: 3, text: "// [rc4l] Starts here" },
    { file: "a.cpp", line: 4, text: "// and goes on." },
    { file: "a.sh", line: 1, text: "# [rc4l] A shell comment. Another." },
    { file: "b.mjs", line: 1, text: "// [rc4l] A file's header." },
    { file: "b.mjs", line: 2, text: "//" },
    { file: "b.mjs", line: 3, text: "// usage: node b.mjs" },
    { file: "a.md", line: 1, text: "// [rc4l] Not code. Ignored." },
    { file: "a.cpp", line: 9, text: "// an upstream comment. With two sentences." },
  ];
  assert.deepEqual(commentIssues(added).map((i) => `${i.file}:${i.line} ${i.why}`),
    ["a.cpp:2 more than one sentence", "a.cpp:3 runs onto the next line", "a.sh:1 more than one sentence"]);
});
