// [rc4l] A mod's script and menus cannot run an unsafe command, and writeini writes only beside the config: exit 0 when the wad's OPEN script, its confirmed option-menu item and its freeform item all fail to register an atexit command, and a named writeini lands beside the config as an .ini.
//
// usage:  node scenarios/commandsafety.mjs [port]
import fs from "node:fs";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, consoleCommand, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { menuNav } from "../src/ui.mjs";
import { resolveEngine } from "../src/launch.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] What each route tries to register; atexit is unsafe and none of these checked it before features/command-safety.
const FROM = { script: "fua-script-ran", option: "fua-option-ran", freeform: "fua-freeform-ran" };
const WRITEINI_NAME = "../fua-commandsafety";

const MENUDEF = `OptionMenu "FuaSafetyMenu"
{
	SafeCommand "Probe", "atexit \\"echo ${FROM.option}\\""
}
FreeformMenu "FuaSafetyFree"
{
	Command
	{
		Text "Probe"
		Pos 100, 100
		Command "atexit \\"echo ${FROM.freeform}\\""
	}
}
`;

export function buildCommandSafetyWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 128, angle: 0, type: 1 });
  const behavior = acs0([...consoleCommand(0), PCD.TERMINATE], [`atexit "echo ${FROM.script}"`]);
  return wad([...mapLumps("MAP01", m, { behavior }), { name: "MENUDEF", data: MENUDEF }]);
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] Opens a menu, moves onto its one item and presses enter `presses` times, the second answering a confirmation yes.
async function activate(c, menu, presses) {
  await cap.exec(c, `openmenu ${menu}`);
  await sleep(300);
  await menuNav(c, ["down", ...Array(presses).fill("enter")], { delay: 200 });
  await sleep(300);
  await cap.exec(c, "closemenu");
}

// [rc4l] The verdict from what atexit lists, the unsafe refusals printed while the menus ran, and where writeini said it saved.
export function judgeCommandSafetyFrom(label, { listed, refusals, saved }) {
  const problems = [];
  for (const [route, text] of Object.entries(FROM)) if (listed.includes(text)) problems.push(`the ${route} registered an atexit command`);
  if (refusals < 2) problems.push(`${refusals} of the 2 menu items were refused as unsafe`);
  if (!saved) problems.push("writeini named no file beside the config");
  else if (path.basename(saved.file) !== "---fua-commandsafety.ini") problems.push(`writeini wrote ${saved.file}`);
  else if (!saved.exists) problems.push(`writeini named ${saved.file}, which is not there`);
  return { line: `${problems.length ? "FAIL" : "PASS"} ${label}: ${problems.length ? problems.join("; ") : "script and menus refused, writeini beside the config"}`, failed: problems.length ? 1 : 0 };
}

export async function judgeCommandSafety(c, inst, label) {
  await cap.exec(c, "fua_render_in_background 1");
  let said = "";
  const off = c.onEvent((ev, data) => { if (ev === "out" && typeof data.text === "string") said += data.text; });
  try {
    await activate(c, "FuaSafetyMenu", 2);
    await activate(c, "FuaSafetyFree", 1);
  } finally { off(); }
  const listed = await cap.exec(c, "atexit");
  const refusals = (said.match(/Cannot execute unsafe command\S* \S*atexit/g) || []).length;
  const m = /Config saved to (.+)\.\s*$/m.exec(await cap.exec(c, `writeini ${WRITEINI_NAME}`));
  let saved = null;
  if (m) {
    const file = path.isAbsolute(m[1]) ? m[1] : path.resolve(path.dirname(resolveEngine()), m[1]);
    saved = { file, exists: fs.existsSync(file) };
    if (saved.exists) fs.rmSync(file);
  }
  return judgeCommandSafetyFrom(label, { listed, refusals, saved });
}

runScenarioIfMain(import.meta.url, () => ({ name: "commandsafety", wad: buildCommandSafetyWad(), maps: ["MAP01"],
  sides: [{ name: "vk", noGl: true }], judgeLaunch: judgeCommandSafety }), compareEachLaunch);
