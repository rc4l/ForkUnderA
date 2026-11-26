// [Doruk-Aksoy] The pictures the UZDoom parity harnesses compare: UZDoom's of a map (src/uzdoom.mjs, its window never activated) and ours
// (fuactl launch: muted, unfocused, windowed, on a port of its own, quit by port).
import path from "node:path";
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { uzdoomScreenshot, ourApiFault } from "../src/uzdoom.mjs";
import { BridgeClient } from "../src/client.mjs";
import { FREEDOOM2 } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const CLI = fileURLToPath(new URL("../src/cli.mjs", import.meta.url));
const ROOT = fileURLToPath(new URL("../../../", import.meta.url));

// [Doruk-Aksoy] UZDoom's screenshot of `map` with `pk3File` loaded, 70 tics in, on `backend`, written under `out`; its path.
export function uzdoomShot(pk3File, map, out, backend = "vulkan") {
  return uzdoomScreenshot({ pk3: pk3File, map, dir: path.join(out, `uzdoom-${backend}-${map}`), backend });
}

// [Doruk-Aksoy] Our launch's answer to the renderer probe, which names the API its backend runs on.
async function ourProbe(port, token) {
  const c = await new BridgeClient().connect(port, { token });
  try { return await cap.exec(c, cap.RENDERER_PROBE, { quietMs: 300 }); } finally { c.close(); }
}

// [Doruk-Aksoy] Our picture of `map` on `api` with `pk3File` loaded once the level is `minTic` tics in (FUACTL_ENGINE picks the binary); its path.
export async function oursShot(pk3File, map, { port, minTic = 10, tag = "parity", api = "vulkan" }) {
  // [Doruk-Aksoy] The IWAD UZDoom runs, named, since a build folder without freedoom2.wad beside it would pick another.
  const launcher = spawn("node", [CLI, "launch", "--port", String(port), "--map", map, "--no-focus", "--iwad", FREEDOOM2, "--file", pk3File,
    "--cvar", "crosshair=0,r_drawplayersprites=0,fua_render_in_background=1", "--arg", `-${api}`], { cwd: ROOT, env: { ...process.env, FUACTL_NO_FOCUS: "1" } });
  launcher.stderr.on("data", (d) => process.stderr.write(d));
  const token = await new Promise((resolve, reject) => {
    let said = "";
    const timer = setTimeout(() => reject(new Error(`our launch never said its token:\n${said}`)), 90000);
    launcher.stdout.on("data", (d) => { said += d; const m = /token=([0-9a-f]+)/.exec(said); if (m) { clearTimeout(timer); resolve(m[1]); } });
    launcher.on("exit", () => { clearTimeout(timer); reject(new Error(`our launch ended:\n${said}`)); });
  });
  const auth = ["--port", String(port), "--token", token];
  try {
    for (let i = 0; i < 90; i++) {
      const r = spawnSync("node", [CLI, "rpc", "sim.tic", ...auth], { encoding: "utf8", cwd: ROOT });
      const tic = /"gametic": (\d+)/.exec(r.stdout || "");
      if (r.status === 0 && /"inlevel": true/.test(r.stdout) && tic && Number(tic[1]) >= Math.max(minTic, 10)) break;
      await new Promise((ok) => setTimeout(ok, 1000));
    }
    const probe = await ourProbe(port, token);
    const fault = ourApiFault(api, probe);
    if (fault) throw new Error(fault);
    const said = spawnSync("node", [CLI, "shot", `${tag}_${api}_${map}`, ...auth], { encoding: "utf8", cwd: ROOT }).stdout;
    const shot = JSON.parse(said.slice(said.indexOf("{")));
    if (!shot.vk) throw new Error(`no ${api} picture: ${JSON.stringify(shot)}`);
    return shot.vk;
  } finally {
    spawnSync("node", [CLI, "quit", ...auth], { encoding: "utf8", cwd: ROOT });
    // [Doruk-Aksoy] The launcher stays up until stopped, which would keep this process alive once the engine is gone.
    launcher.kill();
  }
}
