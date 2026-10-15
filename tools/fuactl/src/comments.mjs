// [rc4l] AGENTS.md's comment rule over a change: each [rc4l] comment it adds is one line of one sentence.
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";

// [rc4l] The comment marker a [rc4l] comment starts with in each kind of file the repo signs.
const MARKERS = [[/\.(cpp|h|c|mm|js|mjs)$/, "//"], [/(\.(sh|yml|cmake)|CMakeLists\.txt)$/, "#"]];

export function markerFor(file) {
  const m = MARKERS.find(([re]) => re.test(file));
  return m ? m[1] : null;
}

// [rc4l] How many sentences text holds: a full stop, question or exclamation mark then a capital or a quote starts another, past e.g., i.e., vs. and etc.
export function sentenceCount(text) {
  const t = text.replace(/\b(e\.g|i\.e|vs|etc|cf)\./gi, "$1").trim();
  return t ? t.split(/(?<=[.!?])\s+(?=[A-Z`"'])/).length : 0;
}

// [rc4l] The lines a unified diff adds, as { file, line, text } with the new file's line numbers.
export function addedLines(diff) {
  const out = [];
  let file = null, line = 0;
  for (const raw of diff.split("\n")) {
    if (raw.startsWith("+++ ")) { file = raw.slice(4).replace(/^b\//, ""); continue; }
    const hunk = /^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@/.exec(raw);
    if (hunk) { line = Number(hunk[1]); continue; }
    if (file === null || raw.startsWith("--- ")) continue;
    if (raw.startsWith("+")) out.push({ file, line: line++, text: raw.slice(1).replace(/\r$/, "") });
    else if (!raw.startsWith("-")) line++;
  }
  return out;
}

// [rc4l] Where added [rc4l] comments break the rule: a second sentence, a comment line with text carrying one on, or prose past a bare marker line, which otherwise only separates it from a usage line.
export function commentIssues(added) {
  const issues = [];
  const at = new Map(added.map((a) => [`${a.file}:${a.line}`, a]));
  for (const a of added) {
    const marker = markerFor(a.file);
    if (!marker) continue;
    const text = a.text.trim();
    const start = `${marker} [rc4l]`;
    if (!text.startsWith(start)) continue;
    if (sentenceCount(text.slice(start.length)) > 1) issues.push({ ...a, why: "more than one sentence" });
    // [rc4l] The comment lines after it up to the next [rc4l] comment or the first line of code.
    const block = [];
    for (let n = a.line + 1, next; (next = at.get(`${a.file}:${n}`)) && next.text.trim().startsWith(marker) && !next.text.trim().startsWith(start); n++) block.push(next.text.trim());
    if (block.length && block[0] !== marker) issues.push({ ...a, why: "runs onto the next line" });
    else if (block.some((t) => t !== marker && /[.!?](\s|$)/.test(t))) issues.push({ ...a, why: "runs on past a blank comment line" });
  }
  return issues;
}

// [rc4l] The lines a change adds since ref: the diff against it and every line of a file git does not track yet, or with staged only what is staged.
export function changeSince(root, ref, { staged = false } = {}) {
  const git = (...args) => execFileSync("git", args, { cwd: root, encoding: "utf8", maxBuffer: 256 * 1024 * 1024 });
  const added = addedLines(git("diff", "--no-color", "-U0", ...(staged ? ["--cached"] : []), ref));
  if (staged) return added;
  for (const file of git("ls-files", "--others", "--exclude-standard").split("\n").filter(Boolean)) {
    if (!markerFor(file)) continue;
    fs.readFileSync(path.join(root, file), "utf8").split("\n").forEach((text, i) => added.push({ file, line: i + 1, text: text.replace(/\r$/, "") }));
  }
  return added;
}
