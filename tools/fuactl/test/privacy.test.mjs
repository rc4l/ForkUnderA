import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { GENERIC_PATTERNS, machinePatterns, patternsPath, privacyIssues, privatePatterns } from "../src/privacy.mjs";

const line = (text, n = 1) => ({ file: "a.cpp", line: n, text });
// [rc4l] Built from pieces, so this file's own lines hold nothing the scan would find.
const who = "some" + "one";

test("a home folder or an email address in an added line is found, shared and build folders and no-reply addresses are not", () => {
  const found = privacyIssues([
    line("// see C:/Users/" + who + "/AppData/x.log", 1),
    line('const p = "C:\\\\Users\\\\' + who + '\\\\x";', 2),
    line("cd /home/" + who + "/src", 3),
    line("open /Users/" + who + "/Library", 4),
    line("mail me at " + who + "@example.org", 5),
    line("save /home/web_user/checkpicture.zds", 6),
    line("C:/Users/Public/Documents", 7),
    line("/home/runner/work", 8),
    line("Co-Authored-By: bot <noreply@example.com>", 9),
    line("1234567+bot@users.noreply.github.com", 10),
    line("https://cdn.jsdelivr.net/npm/react@18.3.1/umd", 11),
    line("git clone git@github.com:org/repo.git", 12),
  ], GENERIC_PATTERNS);
  assert.deepEqual(found.map((f) => `${f.line} ${f.match}`), [
    "1 C:/Users/" + who,
    "2 C:\\\\Users\\\\" + who,
    "3 /home/" + who,
    "4 /Users/" + who,
    "5 " + who + "@example.org",
  ]);
});

test("this machine's user and host names, and the patterns file's lines, are patterns too", () => {
  const patterns = privatePatterns({ user: "QuietUser", host: "pc", fileText: "# notes\n\nBigCard\\s*9000\r\nsecret-project\n" });
  assert.equal(patterns.length, GENERIC_PATTERNS.length + 3);
  const found = privacyIssues([line("built by quietuser"), line("runs on a pc"), line("a bigcard 9000 drew it"), line("SECRET-PROJECT notes"), line("nothing here")], patterns);
  assert.deepEqual(found.map((f) => f.match), ["quietuser", "bigcard 9000", "SECRET-PROJECT"]);
  assert.equal(privatePatterns({ user: "a.b(c)", host: "" }).at(-1).test("x a.b(c) y"), true);
});

test("the patterns file lives outside the repo, and a machine without one scans with the rest", () => {
  const home = fs.mkdtempSync(path.join(os.tmpdir(), "fua-privacy-"));
  try {
    assert.equal(patternsPath(home), path.join(home, ".forkundera", "private-patterns.txt"));
    const without = machinePatterns(home).length;
    fs.mkdirSync(path.dirname(patternsPath(home)), { recursive: true });
    fs.writeFileSync(patternsPath(home), "one\ntwo\n");
    assert.equal(machinePatterns(home).length, without + 2);
  } finally { fs.rmSync(home, { recursive: true, force: true }); }
});
