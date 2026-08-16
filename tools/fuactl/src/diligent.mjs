// [rc4l] Driver for the Diligent (Vulkan) backend port.
//
// The backend is exercised through engine console commands, and driving them by hand means
// remembering an order that actually matters: the level mesh has to be baked (`gl_wallmesh 1`, plus a
// level that has been walked so segs are captured) BEFORE geometry can be uploaded, and the camera is
// snapshotted at upload time. Getting the order wrong reports "no baked geometry", or worse,
// silently benchmarks a stale viewpoint.
//
// Wrapping the sequence means a backend run is one command and always in the right order, and the
// matched GL/Vulkan benchmark is taken the same way every time.
//
// See src/zandronum/src/features/hwrender/diligent/README.md.

import { BridgeClient } from "./client.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] Console output does not come back from `console.exec`, it goes to the engine log -- so the
// caller passes the log path and we read the interesting lines back out of it.
// [rc4l] `sweep` is a list of {mode, file} debug-shot pairs taken from one upload. Diagnosing the
// lighting port meant comparing the shaded picture against raw depth and raw vertex colour, and doing
// that by relaunching once per view cost a build-and-walk cycle each time -- and risked comparing two
// slightly different camera positions, which is exactly how a false conclusion gets published. One
// upload, one camera, N views is the only version of this comparison worth trusting.
export async function diligentRun({ port, token, frames = 500, shot = null, sweep = [], scale = false, bakeAll = false, pause = true, log = () => {} }) {
  const c = new BridgeClient();
  await c.connect(Number(port), { token: token || null });
  await c.waitHello();

  const exec = async (text, waitMs) => {
    await c.rpc("console.exec", { text });
    // `ui exec` leaves the console open; closing it keeps later screenshots and captures clean.
    await c.rpc("input.event", { evtype: 1, subtype: 0, data1: 27, data2: 0 }).catch(() => {});
    if (waitMs) await sleep(waitMs);
  };

  try {
    if (pause) await c.rpc("sim.pause").catch(() => {});

    log("baking level mesh (gl_wallmesh 1 invalidates the wall cache so segs re-capture)");
    await exec("gl_wallmesh 1", 4000);

    if (bakeAll) {
      // [rc4l] Without this the mesh only ever holds what the camera has looked at, so every
      // measurement silently describes one room. The bake runs on the next rendered frame, hence
      // the wait before uploading.
      log("full-level bake (whole BSP, clipping off)");
      await exec("fua_levelmesh_bakeall", 8000);
    }

    log("uploading geometry + snapshotting camera");
    await exec("fua_diligent_scene", 8000);

    if (shot) {
      log(`reading swapchain back to ${shot}`);
      await exec(`fua_diligent_shot ${shot}`, 4000);
    }

    for (const s of sweep) {
      log(`lightmode ${s.mode} -> ${s.file}`);
      await exec(`fua_dg_lightmode ${s.mode}`, 500);
      await exec(`fua_diligent_shot ${s.file}`, 4000);
    }
    if (sweep.length) await exec(`fua_dg_lightmode 1`, 500);

    log(`benchmarking ${frames} frames (Diligent)`);
    await exec(`fua_diligent_bench ${frames}`, Math.max(6000, frames * 8));

    log(`benchmarking ${frames} draws (GL, same geometry)`);
    await exec(`fua_gl_meshbench ${frames}`, 4000);

    if (scale) {
      log("scale probe (GPU time at 1x..100x the visible set)");
      await exec("fua_diligent_scale 60", 30000);
    }
  } finally {
    c.close();
  }
}
