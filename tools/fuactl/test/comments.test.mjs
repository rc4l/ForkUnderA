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

// [rc4l] A paragraph moved with its code kept its first line one sentence and ran on after a bare "//", which the check let through.
test("prose past a bare comment line runs the [rc4l] comment on, and the next [rc4l] comment or code ends it", () => {
  const added = [
    { file: "c.cpp", line: 1, text: "\t// [rc4l] Drop what the old level owned." },
    { file: "c.cpp", line: 2, text: "\t//" },
    { file: "c.cpp", line: 3, text: "\t// The cache is keyed on pointers, so it goes stale" },
    { file: "c.cpp", line: 4, text: "\t// after a map change." },
    { file: "c.cpp", line: 5, text: "\tRelease();" },
    { file: "c.cpp", line: 6, text: "\t// [rc4l] Fine." },
    { file: "c.cpp", line: 7, text: "\t//" },
    { file: "c.cpp", line: 8, text: "\t// [rc4l] Also fine." },
    { file: "c.cpp", line: 9, text: "\tReleaseMore();" },
  ];
  assert.deepEqual(commentIssues(added).map((i) => `${i.file}:${i.line} ${i.why}`), ["c.cpp:1 runs on past a blank comment line"]);
});

// [rc4l] A port's [rc4l] line above the upstream author's own comment it carried in read as one comment run on, 22 times in the Zandronum walk.
test("another author's attributed comment after a [rc4l] comment starts a comment of its own", () => {
  const added = [
    { file: "d.cpp", line: 1, text: "\t// [rc4l] Ported from zandronum@6d3c00306ce0: custom bobbing sets the ranges too." },
    { file: "d.cpp", line: 2, text: "\t// [AK] Adjust the bob style and range if custom bobbing is used too." },
    { file: "d.cpp", line: 3, text: "\tif (cl_usecustombob)" },
    { file: "d.sh", line: 1, text: "# [rc4l] Fine." },
    { file: "d.sh", line: 2, text: "# [Doruk-Aksoy] Theirs." },
    { file: "d.cpp", line: 5, text: "// [rc4l] Starts here" },
    { file: "d.cpp", line: 6, text: "// and goes on." },
    { file: "d.cpp", line: 10, text: "//==========================================================================" },
    { file: "d.cpp", line: 11, text: "// [rc4l] Ported from zandronum@f6f2e6ee1991, inside a function banner." },
    { file: "d.cpp", line: 12, text: "//==========================================================================" },
    { file: "d.cpp", line: 13, text: "// and this is past the banner." },
  ];
  assert.deepEqual(commentIssues(added).map((i) => `${i.file}:${i.line} ${i.why}`), ["d.cpp:5 runs onto the next line"]);
});
