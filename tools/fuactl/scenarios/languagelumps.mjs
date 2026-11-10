// [Doruk-Aksoy] LANGUAGE lumps as UZDoom reads them: exit 0 when '$$' aliases, \x escapes, LMACROS gender texts, a CSV lump and a later file's default text over the base French all print as written, and an alias loop prints as unknown.
//
// usage:  node scenarios/languagelumps.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const LANGUAGE = `[enu default]
ZX_BASE = "Armor";
ZX_ALIAS = "$$zx_base";
ZX_CHAIN = "$$ZX_ALIAS";
ZX_HEX = "A\\x42C";
ZX_LOOPA = "$$ZX_LOOPB";
ZX_LOOPB = "$$ZX_LOOPA";
ZX_MACRO = "Fragg@[zx_end]";
TXT_YES = "Mod yes";
`;
const LANGUAGE_CSV = "identifier,default,fr\nZX_CSV,csv default,csv fr\n";
const LMACROS = "name,language,male,female,neutral,object\nzx_end,en,ed him,ed her,ed them,ed it\n";

export function buildLanguageLumpsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 128, angle: 0, type: 1 });
  return wad([...mapLumps("MAP01", m), { name: "LANGUAGE", data: LANGUAGE }, { name: "LANGUAGE", data: LANGUAGE_CSV },
    { name: "LMACROS", data: LMACROS }]);
}

// [Doruk-Aksoy] Each step's console commands and what print must answer for each label after them.
export const STEPS = [
  { set: ["gender male", "language enu"], want: { ZX_CHAIN: "Armor", ZX_HEX: "ABC", ZX_LOOPA: "ZX_LOOPA unknown", ZX_MACRO: "Fragged him",
    ZX_CSV: "csv default", TXT_YES: "Mod yes" } },
  { set: ["gender female"], want: { ZX_MACRO: "Fragged her" } },
  { set: ["gender other"], want: { ZX_MACRO: "Fragged it" } },
  { set: ["language fr"], want: { TXT_YES: "Mod yes", TXT_NO: "Non", ZX_CSV: "csv fr", ZX_CHAIN: "Armor" } },
];

// [Doruk-Aksoy] The verdict from what print answered for each label at each step.
export function judgeLanguageLumpsFrom(label, answers) {
  const problems = [];
  STEPS.forEach((step, i) => {
    for (const [name, want] of Object.entries(step.want)) {
      const got = (answers[i]?.[name] ?? "").trim();
      if (got !== want) problems.push(`after ${step.set.join("; ")}: ${name} printed ${JSON.stringify(got)}, not ${JSON.stringify(want)}`);
    }
  });
  return { line: `${problems.length ? "FAIL" : "PASS"} ${label}: ${problems.length ? problems.join("; ") : "aliases, escapes, macros, CSV and default text all as written"}`, failed: problems.length ? 1 : 0 };
}

export async function judgeLanguageLumps(c, inst, label) {
  const answers = [];
  for (const step of STEPS) {
    for (const command of step.set) await cap.exec(c, command);
    // [Doruk-Aksoy] The gender macros read the gender the next frame hands the string table.
    await cap.waitTics(c, 2);
    const got = {};
    for (const name of Object.keys(step.want)) got[name] = await cap.exec(c, `print ${name}`);
    answers.push(got);
  }
  return judgeLanguageLumpsFrom(label, answers);
}

runScenarioIfMain(import.meta.url, () => ({ name: "languagelumps", wad: buildLanguageLumpsWad(), maps: ["MAP01"],
  sides: [{ name: "vk", noGl: true }], judgeLaunch: judgeLanguageLumps }), compareEachLaunch);
