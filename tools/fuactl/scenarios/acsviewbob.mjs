// [Doruk-Aksoy] UZDoom's fviewbob userinfo (features/acs-parity): a player flies up and hovers, and exit 0 when, with fviewbob 1, its height and view
// bob as they always have, and with fviewbob 0 neither moves: P_ZMovement's fly bob and P_CalcHeight's both stop.
//
// usage:  node scenarios/acsviewbob.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

// [Doruk-Aksoy] Tics a hover is sampled over, and the least a bobbing height moves in them (the fly bob swings about 1.6 units).
export const SAMPLE_TICS = 40;
export const BOB_SPAN = 0.5;
// [Doruk-Aksoy] How far a still hover may drift, from the rise's last velocity running out.
export const STILL_SPAN = 0.05;

export function buildViewBobWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 512, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  m.thing({ x: 128, y: 128, angle: 90, type: 2 });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] The spread of a list of numbers.
export const span = (xs) => (xs.length ? Math.max(...xs) - Math.min(...xs) : 0);

// [Doruk-Aksoy] What is wrong with a hover's samples, `{ z, view }` per tic, the view as viewz above z: both must bob with fviewbob on, and neither move off.
export function judgeHover(samples, bobs) {
  if (samples.length < SAMPLE_TICS) return [`only ${samples.length} samples of ${SAMPLE_TICS}`];
  const z = span(samples.map((s) => s.z)), view = span(samples.map((s) => s.view));
  if (bobs) return [...(z < BOB_SPAN ? [`the height moved ${z.toFixed(3)} with fviewbob on, not ${BOB_SPAN}`] : []),
    ...(view < BOB_SPAN ? [`the view bobbed ${view.toFixed(3)} with fviewbob on, not ${BOB_SPAN}`] : [])];
  return [...(z > STILL_SPAN ? [`the height moved ${z.toFixed(3)} with fviewbob off`] : []),
    ...(view > STILL_SPAN ? [`the view bobbed ${view.toFixed(3)} with fviewbob off`] : [])];
}

// [Doruk-Aksoy] Flies `player` up off the floor and lets the rise run out, so it hovers: flying friction leaves under 0.001 of the climb's speed after 140 tics.
export async function riseAndHover(player, server, deps = {}) {
  const { exec = cap.exec, waitTics = cap.waitTics } = deps;
  await exec(player, "fly", { quietMs: 100 });
  await exec(player, "+moveup", { quietMs: 40 });
  await waitTics(server, 12);
  await exec(player, "-moveup", { quietMs: 40 });
  await waitTics(server, 140);
}

// [Doruk-Aksoy] `read` once a tic for SAMPLE_TICS tics.
export async function sampleHover(read, server, deps = {}) {
  const { waitTics = cap.waitTics } = deps;
  const out = [];
  for (let i = 0; i < SAMPLE_TICS; i++) {
    const s = await read();
    if (s) out.push(s);
    await waitTics(server, 1);
  }
  return out;
}

// [Doruk-Aksoy] The player's own height and view, from player.camera.
export const cameraOf = (c) => async () => {
  const cam = await c.rpc("player.camera").catch(() => null);
  return cam && typeof cam.z === "number" ? { z: cam.z, view: cam.viewz - cam.z } : null;
};

export const SIDES = [{ name: "on", launchCvars: { fviewbob: 1 } }, { name: "off", launchCvars: { fviewbob: 0 } }];

export async function judgeLaunch(c, inst, label) {
  const bobs = label.endsWith(" on");
  await cap.waitTics(c, 10);
  await riseAndHover(c, c);
  const faults = judgeHover(await sampleHover(cameraOf(c), c), bobs);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: a hovering player's height and view ${bobs ? "bob" : "stay still"} with fviewbob ${bobs ? 1 : 0}`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsviewbob", wad: buildViewBobWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
