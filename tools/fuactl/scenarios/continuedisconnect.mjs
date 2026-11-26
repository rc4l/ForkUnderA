// [Doruk-Aksoy] Continue on a disconnect: a single-player game of Freedoom's MAP01 quits, leaving its Continue save in a user folder; a client sharing that folder joins a netgame on another map and disconnects. Exit 0 when the client comes back up in that save, standing where the game was left, rather than crashing loading it onto the netgame's WAD set before the restart onto Freedoom.
//
// usage:  node scenarios/continuedisconnect.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import { launchInstance, stopInstance } from "../src/launch.mjs";
import { attach } from "../src/session.mjs";
import * as cap from "../src/capture.mjs";
import { scaled } from "../src/timeouts.mjs";

// [Doruk-Aksoy] The netgame's map: one room, nothing like Freedoom's MAP01, so a save of that map loaded onto it links its actors to sectors it does not have.
export function buildContinueDisconnectWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] How near the restored player must stand to where the single-player game was quit.
export const STOOD_WITHIN = 2;

// [Doruk-Aksoy] What is wrong with the client after its disconnect: none came back, it is still online, or it stands somewhere other than the save's spot.
export function judgeReturn(back, saved) {
  if (!back) return ["the client never came back up in a level after its disconnect"];
  if (back.role !== "single") return [`the client is ${back.role} after its disconnect, not single`];
  const apart = Math.hypot(back.x - saved.x, back.y - saved.y);
  return apart <= STOOD_WITHIN ? [] : [`the client stands ${apart.toFixed(1)} units from where the saved game was left`];
}

// [Doruk-Aksoy] The single-player game, quit as a player quits so Continue snapshots it, and where its player stood.
async function leaveSavedGame(port, iwad, userDir) {
  const inst = await launchInstance({ port, iwad, map: "MAP01", userDir, focus: false });
  const c = await attach(inst);
  try {
    await cap.waitInLevel(c);
    await cap.waitTics(c, 35);
    const at = await c.rpc("player.camera", {});
    await cap.exec(c, "quit", { quietMs: 500, maxMs: 15000 }).catch(() => "");
    return at;
  } finally {
    c.close();
    await stopInstance(inst);
  }
}

// [Doruk-Aksoy] The client's state once it is back in a level after the disconnect, or null when it never is.
async function waitForReturn(c) {
  const deadline = Date.now() + scaled(60000);
  let tic = null, net = null;
  while (Date.now() < deadline) {
    await cap.sleep(500);
    tic = await c.rpc("sim.tic").catch(() => null);
    net = await c.rpc("net.state", {}).catch(() => null);
    if (tic?.inlevel && net?.role === "single") {
      await cap.waitTics(c, 10);
      const at = await c.rpc("player.camera", {});
      return { role: net.role, x: at.x, y: at.y };
    }
  }
  console.log(`continuedisconnect: last seen ${JSON.stringify({ tic, net })}`);
  return null;
}

export async function judgeNetplay(set, label, { iwad }) {
  const userDir = fs.mkdtempSync(path.join(os.tmpdir(), "continuedisconnect-"));
  const saved = await leaveSavedGame(set.basePort + 9, iwad, userDir);
  if (!fs.existsSync(path.join(userDir, "continue", "offline.txt"))) return { lines: [`FAIL ${label}: the single-player quit left no Continue record in ${userDir}`], failed: 1 };
  const returner = await addClient(set, { role: "returner", userDir });
  await cap.exec(returner.c, "disconnect", cap.SLOW_COMMAND);
  const faults = judgeReturn(await waitForReturn(returner.c), saved);
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: back in the saved game where it was left`], failed: faults.length ? 1 : 0 };
}

// [Doruk-Aksoy] On the snapshot netcode with legacy spawns and per-actor commands still sent beside it (level 2), which a netgame runs once its server turns it on.
export const SNAPSHOT_NETCODE = { fua_snap: 1, fua_snap_send: 1, fua_snap_nolegacy: 2 };
export const CONFIGS = [{ name: "lan", players: 1, serverCvars: SNAPSHOT_NETCODE }];

runScenarioIfMain(import.meta.url, () => ({ name: "continuedisconnect", wad: buildContinueDisconnectWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
