// [rc4l] Comparisons that need the engine driven through a sequence: several maps, or a door caught
// mid-swing. Both were shell scripts, and both encode a lesson about WAITING that a sleep gets wrong.
import fs from "node:fs";
import path from "node:path";
import { BridgeClient } from "./client.mjs";
import { launchInstance, stopInstance, resolveEngine } from "./launch.mjs";
import { exec, waitTics, sandbox } from "./capture.mjs";
import { shotPair } from "./shot.mjs";
import * as ui from "./ui.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] Wait for THIS map's upload, do not guess at it with a sleep.
//
// A fixed sleep captured the frame before the new mesh landed, so the backend was drawing the
// PREVIOUS map while GL drew the new one -- and the pair scored as a catastrophic rendering bug.
// MAP15 came out at mean|d| 41.6 that way and 8.9 once the wait was real, which is the difference
// between "the sky is broken" and "the sky is fine".
//
// The upload announces itself on the console, and console output arrives as bridge events, so this
// is a matter of listening rather than of counting lines in a log file on disk.
export async function changeMap(c, map, { timeoutMs = 90000 } = {}) {
  let uploaded = false;
  const off = c.onEvent((ev, d) => {
    if (ev === "out" && typeof d.text === "string" && /^vulkan: uploaded/m.test(d.text)) uploaded = true;
  });
  try {
    await c.rpc("console.exec", { text: `map ${map}` });
    const deadline = Date.now() + timeoutMs;
    while (!uploaded && Date.now() < deadline) await sleep(200);
    if (!uploaded) throw new Error(`${map}: no mesh upload after ${Math.round(timeoutMs / 1000)}s`);
    await waitTics(c, 20);   // let the first drawn frames settle
  } finally { off(); }
}

// [rc4l] Matched GL/Vulkan pairs across several maps, ranked by how much they disagree.
//
// Deciding what to port next off a feature list is guessing: the list says portals, 3D floors,
// decals, models, and says nothing about which of them a player actually walks past. This walks a
// spread of stock maps and prints a number per map, so the next thing to fix is the worst number
// rather than the most interesting-sounding entry. It also exercises the per-level re-bake, which
// is the one path a single-map test can never reach.
export async function sweep({
  maps = ["MAP01", "MAP02", "MAP07", "MAP11", "MAP15", "MAP27"],
  port = 7902, iwad = "doom2.wad", outDir = "F:/ForkUnderA/dist-windows/sweep",
  log = console.error,
} = {}) {
  fs.mkdirSync(outDir, { recursive: true });
  const inst = await launchInstance({
    port, iwad, map: maps[0], cvars: { sv_nomonsters: 1, fua_vulkan: 1 },
  });
  const c = new BridgeClient();
  const pairs = [];
  try {
    for (let i = 0; i < 120; i++) {
      try { await c.connect(inst.port, { token: inst.token, timeoutMs: 1500 }); await c.waitHello(3000); break; }
      catch { await sleep(500); }
    }
    await exec(c, "god");
    for (const m of maps) {
      await changeMap(c, m);
      const out = await shotPair(c, m, { engineBin: resolveEngine(), outDir });
      log(`${m}: ${out.gl ? "gl" : "NO GL"} ${out.vk ? "vk" : "NO VK"}`);
      if (out.gl && out.vk) pairs.push(out);
    }
  } finally {
    c.close();
    await stopInstance(inst);
  }
  return pairs;
}

// [rc4l] A door caught MID-ANIMATION, in both renderers.
//
// A still level says nothing about moving geometry. The one bug this was written for -- a shut door
// staying painted across an open doorway -- was only visible with the sim frozen part-way through
// the raise, and reproducing it by hand took a dozen ordered calls, which is exactly how the first
// "fix" got declared working when it was not.
//
// A BEFORE pair as well as an after one, because without it the test cannot tell "Vulkan tracked
// the motion" from "nothing moved and both renderers drew the same still wall" -- which is what the
// very first run of this actually did, on a lift in a dark corridor, and it agreed perfectly.
export async function doorShot(c, tag, {
  at = { x: 992, y: 1000, angle: 90 }, midTics = 12, engineBin, outDir,
} = {}) {
  await c.rpc("player.setpos", { x: at.x, y: at.y, z: at.z ?? 0, angle: at.angle, pitch: at.pitch || 0 });
  await waitTics(c, 10);
  const before = await shotPair(c, `${tag}0`, { engineBin, outDir });

  await exec(c, "+use", { quietMs: 60 });
  await waitTics(c, 2);
  await exec(c, "-use", { quietMs: 60 });
  await waitTics(c, midTics);          // part-way through the raise, not open and not shut
  const mid = await shotPair(c, `${tag}1`, { engineBin, outDir });
  return { before, mid };
}

// [rc4l] What is the surface under the crosshair, and what does the mesh hold for it?
//
// The loop this replaces: screenshot, guess a cause, rebuild, ask someone to walk back there and
// look again. Four rounds of that on one pane of glass. The question every round was really asking
// is "what does the mesh say about THIS surface", and it is one command against a running instance.
export async function look(c, at = null) {
  if (at) {
    await c.rpc("player.setpos", { x: at.x, y: at.y, z: at.z, angle: at.angle, pitch: at.pitch || 0 });
    await waitTics(c, 6);
  }
  return exec(c, "fua_look");
}
