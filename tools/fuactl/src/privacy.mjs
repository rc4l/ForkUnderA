// [rc4l] What a change adds that could identify whoever made it: their home folder's path, an email address, this machine's user and host names, and the patterns a file outside the repo lists.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

// [rc4l] Shapes that name a person in any repo: a Windows or POSIX home folder (shared and build users aside) and an email address (no-reply ones, git remotes and package versions aside).
export const GENERIC_PATTERNS = [
  /\b[A-Za-z]:[\\/]{1,2}Users[\\/]{1,2}(?!Public\b|Default\b)[^\\/\s"'`]+/i,
  /\/(?:Users|home)\/(?!web_user\b|runner\b|Shared\b|Public\b|Default\b)[A-Za-z][^/\s"'`$]*/,
  /(?<![\w.+-])(?!git@)(?![\w.+-]*@?[\w.-]*noreply)[\w.+-]+@[\w-]+(?:\.[\w-]+)*\.[A-Za-z]{2,}\b/i,
];

// [rc4l] Where the patterns that name this person live, one regular expression a line ('#' starts a note), kept outside the repo so the list never names them itself.
export function patternsPath(home = os.homedir()) {
  return path.join(home, ".forkundera", "private-patterns.txt");
}

// [rc4l] Literal text as a pattern matching it anywhere, in any case.
const literal = (text) => new RegExp(text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "i");

// [rc4l] Every pattern a scan uses: the generic ones, this machine's user and host names when long enough not to match words, and the file's lines.
export function privatePatterns({ user = os.userInfo().username, host = os.hostname(), fileText = "" } = {}) {
  const own = [user, host].filter((n) => n && n.length >= 4).map(literal);
  const listed = fileText.split(/\r?\n/).map((l) => l.trim()).filter((l) => l && !l.startsWith("#")).map((l) => new RegExp(l, "i"));
  return [...GENERIC_PATTERNS, ...own, ...listed];
}

// [rc4l] The patterns for this machine, the file's included when it exists.
export function machinePatterns(home = os.homedir()) {
  const file = patternsPath(home);
  return privatePatterns({ fileText: fs.existsSync(file) ? fs.readFileSync(file, "utf8") : "" });
}

// [rc4l] Each added line a pattern matches, with what it matched.
export function privacyIssues(added, patterns) {
  const issues = [];
  for (const a of added) {
    for (const p of patterns) {
      const m = p.exec(a.text);
      if (m) { issues.push({ ...a, match: m[0] }); break; }
    }
  }
  return issues;
}
