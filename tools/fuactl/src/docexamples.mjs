// [Doruk-Aksoy] The ACS examples in docs/modding/acs: each ```bcs block is a whole library, and the HTML comment after it names the console steps that run it
// and the log each must print, so a scenario can compile every example with zt-bcc and check it in the engine.
//
//   ```bcs
//   #library "fuahome"
//   ...
//   ```
//   <!-- example
//   pukename Place 100 680 0
//   pukename Seek => /homing on 31/
//   -->
//
// Each line is a step: a console command, then ` => /regex/` when its output must match; `@changemap MAP01` changes map and waits for it. Examples compile
// with `-target fua`, as they are ZandroX's. `<!-- example: compile -->` only compiles the block.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const DOCS = path.resolve(HERE, "../../../docs/modding/acs");

// [Doruk-Aksoy] One step of a marker: its command, and the pattern its output must match or null.
export function parseStep(text) {
  const m = /^(.*?)\s*=>\s*\/(.*)\/([a-z]*)$/.exec(text.trim());
  return m ? { command: m[1].trim(), want: new RegExp(m[2], m[3]) } : { command: text.trim(), want: null };
}

// [Doruk-Aksoy] Every ```bcs block of a page with its library name, the line it starts on and its steps; a block without a marker has steps null.
export function docExamples(markdown, page = "") {
  const lines = String(markdown).replace(/\r\n/g, "\n").split("\n");
  const out = [];
  for (let i = 0; i < lines.length; i++) {
    if (lines[i] !== "```bcs") continue;
    const start = i + 1;
    let end = start;
    while (end < lines.length && lines[end] !== "```") end++;
    const code = lines.slice(start, end).join("\n") + "\n";
    let next = end + 1;
    while (next < lines.length && lines[next].trim() === "") next++;
    const library = /#library\s+"([^"]+)"/.exec(code)?.[1] || null;
    let steps = null, compileOnly = false;
    if (/^<!--\s*example:\s*compile\s*-->$/.test(lines[next] || "")) { steps = []; compileOnly = true; }
    else if (/^<!--\s*example\s*$/.test(lines[next] || "")) {
      steps = [];
      for (next++; next < lines.length && lines[next].trim() !== "-->"; next++) if (lines[next].trim()) steps.push(parseStep(lines[next]));
    }
    out.push({ page, line: start, code, library, steps, compileOnly });
    i = end;
  }
  return out;
}

// [Doruk-Aksoy] Every example in the pages of `dir`.
export function allDocExamples(dir = DOCS, read = fs.readFileSync, list = fs.readdirSync) {
  return list(dir).filter((f) => f.endsWith(".md")).sort().flatMap((f) => docExamples(read(path.join(dir, f), "utf8"), f));
}

// [Doruk-Aksoy] What is wrong with a page's examples as written: no marker, no library name, a name another example has, or a library name zt-bcc refuses.
export function exampleFaults(examples) {
  const faults = [], seen = new Map();
  for (const e of examples) {
    const at = `${e.page}:${e.line}`;
    if (e.steps === null) faults.push(`${at}: no <!-- example: ... --> marker after the block`);
    if (!e.library) faults.push(`${at}: no #library line`);
    else if (e.library.length > 8) faults.push(`${at}: library "${e.library}" is longer than 8 characters`);
    else if (seen.has(e.library)) faults.push(`${at}: library "${e.library}" is also ${seen.get(e.library)}'s`);
    else seen.set(e.library, at);
  }
  return faults;
}

// [Doruk-Aksoy] The zt-bcc to compile with and its lib folder: FUACTL_BCC (and FUACTL_BCC_LIB), else the zt-bcc checkout beside the repository.
export function bccPaths(env = process.env) {
  const exe = env.FUACTL_BCC || path.resolve(HERE, "../../../../zt-bcc/build/zt-bcc.exe");
  return { exe, lib: env.FUACTL_BCC_LIB || path.resolve(path.dirname(exe), "../lib") };
}

// [Doruk-Aksoy] Compiles `code` into `dir/name.o` and returns its bytes, or throws with what zt-bcc said; warnings count as failures, as zt-bcc's own doc test has it.
export function compileBcs(code, name, dir, { exe, lib, target = "fua" } = bccPaths(), run = spawnSync) {
  fs.mkdirSync(dir, { recursive: true });
  const source = path.join(dir, `${name}.bcs`), object = path.join(dir, `${name}.o`);
  fs.writeFileSync(source, code);
  const r = run(exe, ["-i", lib, "-target", target, source, object], { encoding: "utf8", cwd: dir });
  const said = `${r.stdout || ""}${r.stderr || ""}`.trim();
  if (r.status !== 0 || /warning|error/i.test(said)) throw new Error(`${name} doesn't compile cleanly:\n${said}`);
  return fs.readFileSync(object);
}
