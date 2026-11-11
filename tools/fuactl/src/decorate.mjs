// [Doruk-Aksoy] A template's DECORATE checked against the engine's own flag table before anything launches, since the engine stops on a modal error box for a script error.
import fs from "node:fs";
import { fileURLToPath } from "node:url";

const FLAG_TABLE = fileURLToPath(new URL("../../../src/zandronum/src/thingdef/thingdef_data.cpp", import.meta.url));

// [Doruk-Aksoy] Every flag name thingdef_data.cpp defines, upper-cased: DEFINE_FLAG and DEFINE_FLAG2 name it second, the deprecated and dummy ones first.
export function engineFlagNames(src = fs.readFileSync(FLAG_TABLE, "utf8")) {
  const named = [...src.matchAll(/DEFINE_FLAG2?\s*\(\s*[^,]+,\s*([A-Za-z0-9_]+)\s*,/g)];
  const bare = [...src.matchAll(/DEFINE_(?:DEPRECATED|DUMMY)_FLAG\s*\(\s*([A-Za-z0-9_]+)\s*\)/g)];
  return new Set([...named, ...bare].map((m) => m[1].toUpperCase()));
}

// [Doruk-Aksoy] What the engine would refuse in this DECORATE: a flag it does not know (a dotted one by its last part), and a state block on one line, whose Stop it reads as a frame parameter.
export function decorateFaults(text, flags = engineFlagNames()) {
  const faults = [];
  String(text).split(/\r?\n/).forEach((line, i) => {
    const flag = /^\s*[+-]([A-Za-z0-9_.]+)\s*$/.exec(line);
    if (flag && !flags.has(flag[1].split(".").pop().toUpperCase())) faults.push(`line ${i + 1}: unknown flag ${flag[1]}`);
    if (/\{[^}]*\b(Stop|Loop|Wait|Fail)\b[^}]*\}/i.test(line)) faults.push(`line ${i + 1}: a state block on one line, which reads its ${/\b(Stop|Loop|Wait|Fail)\b/i.exec(line)[1]} as a frame parameter`);
  });
  return faults;
}
