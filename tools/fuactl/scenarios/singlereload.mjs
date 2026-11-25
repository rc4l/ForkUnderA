// [Doruk-Aksoy] Single player on netonetime's map: a switch lowers a floor and lines open a door and raise a platform, and the moved sectors are drawn as they first were after a save is loaded over a restarted map, and after the map is restarted and the same actions finish out of view; exit 0 when every picture matches the first.
//
// usage:  node scenarios/singlereload.mjs [port] [--engine PATH], or fuactl scenarios singlereload
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { runScenarioIfMain, FREEDOOM2 } from "../src/template.mjs";
import { buildNetOneTimeWad, oneTimeFaults, actOnce, lookAtActions, worstBlock, LOOK_ANGLES, PICTURE_CEILING } from "./netonetime.mjs";

export const CVARS = { vid_maxfps: 0, vid_vsync: 0, i_pauseinbackground: 0, fua_render_in_background: 1, snd_sfxvolume: 0, snd_musicvolume: 0, show_messages: 0 };

// [Doruk-Aksoy] One stage's pictures against the first, each angle's worst block judged against PICTURE_CEILING.
export function judgeStage(stage, reference, pictures, decode) {
  const lines = [];
  let failed = 0;
  LOOK_ANGLES.forEach((angle, i) => {
    if (!reference[i] || !pictures[i]) { lines.push(`FAIL ${stage} ${angle}: no capture`); failed++; return; }
    const worst = worstBlock(decode(reference[i]), decode(pictures[i]));
    const bad = worst.d > PICTURE_CEILING;
    if (bad) failed++;
    lines.push(`${bad ? "FAIL" : "PASS"} ${stage} ${angle}: the moved sectors drawn ${bad ? "otherwise than" : "as"} the first time (worst block ${worst.d.toFixed(1)} at ${worst.x},${worst.y})`);
  });
  return { lines, failed };
}

export async function runSingleReload({ port, engine, outDir }) {
  const { launchInstance, stopInstance } = await import("../src/launch.mjs");
  const { attach } = await import("../src/session.mjs");
  const cap = await import("../src/capture.mjs");
  const { decodePNG } = await import("../src/pngcore.mjs");
  const wadPath = path.join(outDir, "singlereload.wad");
  fs.writeFileSync(wadPath, buildNetOneTimeWad());
  const inst = await launchInstance({ port, engine, map: "MAP01", iwad: FREEDOOM2, focus: false, cvars: CVARS, extraArgs: ["-file", wadPath] });
  const c = await attach(inst);
  const me = { c };
  const lines = [];
  let failed = 0;
  // [Doruk-Aksoy] The map's start again, its player at its start facing the switch.
  const restart = async () => { await cap.exec(c, "map MAP01", cap.SLOW_COMMAND); await cap.waitInLevel(c); await cap.waitTics(c, 35); await cap.exec(c, "god", { quietMs: 100 }); };
  try {
    await cap.waitInLevel(c);
    await cap.exec(c, "god", { quietMs: 100 });
    await cap.waitTics(c, 35);
    await actOnce(c, me);
    const faults = oneTimeFaults(await (await import("../src/netdiff.mjs")).readWorld(c));
    if (faults.length) return { lines: [`FAIL the actions did not happen: ${faults.join("; ")}`], failed: 1 };
    const reference = await lookAtActions(c, me, path.join(outDir, "first"));
    await cap.exec(c, "save fuactl_singlereload.zds", { quietMs: 400 });
    await cap.waitTics(c, 10);
    // [Doruk-Aksoy] A save loaded over the same map restarted, its planes restored into a level baked from the map.
    await restart();
    await cap.exec(c, "load fuactl_singlereload.zds", { quietMs: 400 });
    await cap.waitInLevel(c);
    await cap.waitTics(c, 35);
    for (const r of [judgeStage("loaded over a restart", reference, await lookAtActions(c, me, path.join(outDir, "loaded")), decodePNG)]) { lines.push(...r.lines); failed += r.failed; }
    // [Doruk-Aksoy] The map restarted and the same actions taken with the player facing away, so the moves finish before any view reaches them.
    await restart();
    await actOnce(c, me);
    for (const r of [judgeStage("restarted, moved out of view", reference, await lookAtActions(c, me, path.join(outDir, "again")), decodePNG)]) { lines.push(...r.lines); failed += r.failed; }
  } finally {
    c.close();
    await stopInstance(inst);
  }
  return { lines, failed };
}

// [Doruk-Aksoy] Run by `fuactl scenarios singlereload` or directly, each run's frames kept in a temp folder named in its last line.
export async function compareSingleReload({ port = 7796 } = {}) {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "singlereload-"));
  const r = await runSingleReload({ port, engine: process.env.FUACTL_ENGINE, outDir });
  return { lines: [...r.lines, `frames in ${outDir}`], failed: r.failed };
}

runScenarioIfMain(import.meta.url, () => ({ name: "singlereload" }), compareSingleReload);
