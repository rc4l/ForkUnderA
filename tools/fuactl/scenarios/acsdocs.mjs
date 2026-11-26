// [Doruk-Aksoy] The ACS examples in docs/modding/acs (src/docexamples.mjs): exit 0 when every ```bcs block compiles cleanly with zt-bcc, and each one's marker
// steps, run on a fresh map with the examples loaded as libraries, print what the marker wants. Skipped when no zt-bcc is found (FUACTL_BCC).
//
// The map: a 1024 square room, the player at (64, 64) facing east, barrels 11-13 at (300..500, 300), and imps 31-33 at (600..800, 680) facing east, deaf.
//
// usage:  node scenarios/acsdocs.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { allDocExamples, exampleFaults, bccPaths, compileBcs } from "../src/docexamples.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const DRIVER = fileURLToPath(new URL("../fixtures/acsdocs/fuadrv.bcs", import.meta.url));
export const TIDS = { barrels: [11, 12, 13], imps: [31, 32, 33] };

export function buildDocMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 64, angle: 0, type: 1 });
  TIDS.barrels.forEach((id, i) => m.thing({ x: 300 + 100 * i, y: 300, angle: 0, type: THING.ExplosiveBarrel, id }));
  TIDS.imps.forEach((id, i) => m.thing({ x: 600 + 100 * i, y: 680, angle: 0, type: THING.DoomImp, id, ambush: true }));
  return m;
}

// [Doruk-Aksoy] The pk3: the map, the driver and every example compiled into acs/, all loaded by LOADACS.
export function buildDocsPk3(examples, compile = compileBcs, dir = fs.mkdtempSync(path.join(os.tmpdir(), "fua-acsdocs-"))) {
  const libs = [{ name: "fuadrv", data: compile(fs.readFileSync(DRIVER, "utf8"), "fuadrv", dir) },
    ...examples.map((e) => ({ name: e.library, data: compile(e.code, e.library, dir) }))];
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildDocMap())) },
    ...libs.map((l) => ({ name: `acs/${l.name}.o`, data: l.data })), { name: "LOADACS.txt", data: libs.map((l) => l.name).join("\n") + "\n" }]);
}

// [Doruk-Aksoy] What is wrong with one example's run: each step whose output misses what it wants; `@changemap M` waits until the new level is up.
export async function runExample(e, exec, enter = async () => {}) {
  const faults = [];
  for (const step of e.steps) {
    const to = /^@changemap\s+(\S+)$/.exec(step.command);
    const out = to ? await enter(`changemap ${to[1]}`) : await exec(step.command);
    if (step.want && !step.want.test(out)) faults.push(`${e.page}:${e.line} ${step.command}: ${String(out).trim().split("\n").slice(-2).join(" | ") || "nothing"} (want ${step.want})`);
  }
  return faults;
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE, sv_nomonsters: 0 } }];

// [Doruk-Aksoy] Each example runs on a fresh map, the player a god no imp targets.
export function judgeWith(examples) {
  return async (c, inst, label) => {
    const exec = (text) => cap.exec(c, text, { quietMs: 400 });
    const enter = async (command) => {
      const out = await exec(command);
      await cap.waitInLevel(c, undefined, { pressThrough: true });
      await cap.waitTics(c, 5);
      return out;
    };
    const faults = [];
    let ran = 0;
    for (const e of examples.filter((x) => x.steps.length)) {
      await enter("map MAP01");
      for (const cheat of ["god", "notarget"]) await exec(cheat);
      faults.push(...(await runExample(e, exec, enter)));
      ran++;
    }
    return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${examples.length} examples compile cleanly and ${ran} print what their docs say`, failed: faults.length ? 1 : 0 };
  };
}

// [Doruk-Aksoy] Compiles the examples once zt-bcc is found, then runs them; a page's marker faults fail it before any launch.
export async function compareDocs(opts, { examples = allDocExamples(), paths = bccPaths(), compare = compareEachLaunch } = {}) {
  if (!fs.existsSync(paths.exe)) return { lines: [`SKIP acsdocs: no zt-bcc at ${paths.exe} (set FUACTL_BCC)`], failed: 0 };
  const faults = exampleFaults(examples);
  if (faults.length) return { lines: faults.map((f) => `FAIL ${f}`), failed: faults.length };
  return compare({ ...opts, wad: buildDocsPk3(examples, (code, name, dir) => compileBcs(code, name, dir, paths)), judgeLaunch: judgeWith(examples) });
}

compareDocs.judgedAlone = true;

runScenarioIfMain(import.meta.url, () => ({ name: "acsdocs", maps: ["MAP01"], sides: SIDES }), compareDocs);
