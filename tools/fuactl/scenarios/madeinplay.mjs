// [rc4l] What a player first does in a level: floor and wall marks, the automap, the menu, a horizon and a reflective floor coming into view, the status bar filling: exit 0 when none of it makes a shader or pipeline, each a compile the frame waits on.
// [Doruk-Aksoy] And a post-process shader of each stage turned on, whose programs are made at launch.
//
// usage:  node scenarios/madeinplay.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import * as cap from "../src/capture.mjs";
import { parseConsoleJson } from "../src/telemetry.mjs";
import { statOf } from "./ticoverload.mjs";

const FLOOR_TAG = 7;

// [Doruk-Aksoy] A post-process shader for each stage, off until a step turns them on; a wad finds its lumps by their short names.
const POST_STAGES = ["beforebloom", "scene", "screen"];
const POST_LUMPS = [
  { name: "GLDEFS", data: POST_STAGES.map((s) => `HardwareShader PostProcess ${s}\n{\n\tName "mip_${s}"\n\tShader "PPINVERT" 330\n}\n`).join("") },
  { name: "PPINVERT", data: "void main()\n{\n\tFragColor = vec4(1.0 - texture(InputTexture, TexCoord).rgb, 1.0);\n}\n" },
];
const postSteps = (v) => POST_STAGES.flatMap((s) => [`shaderenable mip_${s} ${v}`, "wait:300"]);

// [rc4l] A long room whose north end, behind the player at the start, holds a reflective floor and a horizon line.
export function buildMadeInPlayWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 2048, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => (edge === "north" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } } : { side: { texturemiddle: "STARTAN2" } }));
  m.block(128, 1200, 896, 1900, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT1", textureceiling: "CEIL3_5", lightlevel: 192, id: FLOOR_TAG }, room);
  m.thing({ x: 512, y: 200, angle: 270, type: 1 });
  const script = acs0([...callSpecial(SPECIAL.Sector_SetPlaneReflection, [FLOOR_TAG, 160, 0]), PCD.TERMINATE]);
  return wad([...mapLumps("MAP01", m, { behavior: script }), ...POST_LUMPS]);
}

// [rc4l] Each step: what it does, then the console steps and the view it is done from.
export const STEPS = [
  { action: "floor marks", at: { x: 512, y: 200, z: 0, yaw: 270, pitch: 60 }, commands: ["+attack", "wait:500", "-attack"] },
  { action: "wall marks", at: { x: 512, y: 200, z: 0, yaw: 270, pitch: 0 }, commands: ["+attack", "wait:500", "-attack"] },
  { action: "automap", commands: ["togglemap", "wait:500", "togglemap"] },
  { action: "menu", commands: ["menu_main", "wait:500", "closemenu"] },
  { action: "horizon and reflective floor", at: { x: 512, y: 600, z: 0, yaw: 90, pitch: 10 }, commands: ["wait:500"] },
  { action: "status bar", commands: ["give all", "wait:500"] },
  // [Doruk-Aksoy] `ran` names the counter that must show the step drew at all, so a shader left out cannot pass it.
  { action: "post-process shaders on and off", commands: [...postSteps(1), ...postSteps(0)], ran: "gpu/post/passes" },
];

// [rc4l] The verdict: every step made no shader and no pipeline.
export function judgeMadeInPlay(label, steps) {
  if (!steps.length || steps.some((s) => !Number.isFinite(s.shaders) || !Number.isFinite(s.pipelines)))
    return { line: `FAIL ${label}: no gpu/made/shaders or gpu/made/pipelines to read`, failed: 1 };
  const idle = steps.filter((s) => s.ran !== undefined && !(s.ran > 0));
  if (idle.length) return { line: `FAIL ${label}: ${idle.map((s) => s.action).join(", ")} never drew`, failed: 1 };
  const made = steps.filter((s) => s.shaders > 0 || s.pipelines > 0);
  if (made.length) return { line: `FAIL ${label}: made in play by ${made.map((s) => `${s.action} (${s.shaders} shaders, ${s.pipelines} pipelines)`).join(", ")}`, failed: 1 };
  return { line: `PASS ${label}: ${steps.length} steps made no shader or pipeline`, failed: 0 };
}

// [rc4l] The shaders and pipelines made so far, after a wait past their gauges' half-second interval.
async function madeSoFar(c) {
  await cap.sleep(700);
  const rows = parseConsoleJson(await cap.exec(c, "fua_stats json gpu/made/"));
  return { shaders: statOf(rows, "gpu/made/shaders", "last"), pipelines: statOf(rows, "gpu/made/pipelines", "last") };
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 35);
  let before = await madeSoFar(c);
  const steps = [];
  for (const step of STEPS) {
    if (step.at) await c.rpc("player.setpos", { x: step.at.x, y: step.at.y, z: step.at.z, angle: step.at.yaw, pitch: step.at.pitch });
    for (const command of step.commands) {
      if (command.startsWith("wait:")) await cap.sleep(Number(command.slice(5)));
      else await cap.exec(c, command, { quietMs: 60 });
    }
    const ran = step.ran ? statOf(parseConsoleJson(await cap.exec(c, `fua_stats json ${step.ran}`)), step.ran, "max") : undefined;
    const after = await madeSoFar(c);
    steps.push({ action: step.action, shaders: after.shaders - before.shaders, pipelines: after.pipelines - before.pipelines, ran });
    before = after;
  }
  return judgeMadeInPlay(label, steps);
}

runScenarioIfMain(import.meta.url, () => ({ name: "madeinplay", wad: buildMadeInPlayWad(), maps: ["MAP01"], sides: [{ name: "vk" }], judgeLaunch }), compareEachLaunch);
