// [Doruk-Aksoy] A netgame client's tic stream against its CPU path on a still scene, at joining, after its own reconnect and after the server restarts the map: exit 0 when each time both draw the same picture and every corpse resting on the glowing floor is a tic sprite (steady through the tic), none left to the frame.
//
// usage:  node scenarios/netgpusprites.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { joinGame } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { captureVulkan } from "../src/shot.mjs";
import { decodePNG } from "../src/pngcore.mjs";
import { scaled } from "../src/timeouts.mjs";
import { CEILING, worstBlocks, parseTicStats } from "./gpusprites.mjs";

// [Doruk-Aksoy] Doom's corpse things that draw (a dead lost soul has no sprite), a row on the plain floor and a row on the glowing one, every one in the start's view.
export const CORPSES = [10, 12, 15, 18, 19, 20, 21, 22, 24];
// [Doruk-Aksoy] A glow on a flat that does not animate, so two captures a moment apart see the same floor.
export const GLDEFS = "glow\n{\n\tflats\n\t{\n\t\tFLAT14\n\t}\n}\n";

export function buildNetGpuSpritesWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1536, 1536, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 200 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(200, 700, 1336, 1100, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5", lightlevel: 200 }, room);
  CORPSES.forEach((type, i) => {
    m.thing({ x: 260 + i * 110, y: 900, type });
    m.thing({ x: 260 + i * 110, y: 560, type });
  });
  m.thing({ x: 768, y: 200, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m), { name: "GLDEFS", data: GLDEFS }]);
}

// [Doruk-Aksoy] The actors fua_dg_ticstats's last tic derive left out for standing on a glowing floor.
export function parseGlowLeft(text) {
  const m = /ticwhy .*\bglow (\d+)/.exec(text || "");
  return m ? Number(m[1]) : null;
}

// [Doruk-Aksoy] What is wrong with one moment: the two pictures apart, no tic stream, too few tic sprites, or a corpse on the glow left to the frame.
export function judgeStill({ worst, stats, glowLeft }, when, ceiling = CEILING) {
  const faults = [];
  if (!(worst <= ceiling)) faults.push(`${when}, the tic stream and the CPU path drew ${worst.toFixed(1)} apart`);
  if (!stats) faults.push(`${when}, no tic stream`);
  else if (stats.shown < CORPSES.length * 2) faults.push(`${when}, ${stats.shown} tic sprites shown, not the ${CORPSES.length * 2} corpses`);
  if (glowLeft === null || glowLeft > 0) faults.push(`${when}, ${glowLeft ?? "?"} corpses on the glowing floor left to the frame`);
  return faults;
}

// [Doruk-Aksoy] The client's view drawn by the tic stream, then by the CPU path, its stats read first.
async function moment(c, outDir, name) {
  await cap.waitTics(c, 70);
  const text = await cap.exec(c, "fua_dg_ticstats", { quietMs: 250 });
  const gpu = path.join(outDir, `${name}-gpu.png`), cpu = path.join(outDir, `${name}-cpu.png`);
  await cap.sleep(300);
  if (!(await captureVulkan(c, gpu))) throw new Error(`no capture ${gpu}`);
  await cap.exec(c, "fua_dg_gpusprites 0", { quietMs: 150 });
  await cap.sleep(400);
  if (!(await captureVulkan(c, cpu))) throw new Error(`no capture ${cpu}`);
  await cap.exec(c, "fua_dg_gpusprites 1", { quietMs: 150 });
  await cap.sleep(200);
  return { worst: worstBlocks(decodePNG(gpu), decodePNG(cpu))[0].d, stats: parseTicStats(text), glowLeft: parseGlowLeft(text) };
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const client = set.clients[0];
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "netgpusprites-"));
  const faults = [];
  // [Doruk-Aksoy] The console's notify lines would differ between the two captures.
  await cap.exec(client.c, "show_messages 0", { quietMs: 150 });
  faults.push(...judgeStill(await moment(client.c, outDir, "joined"), "on joining"));
  await cap.exec(client.c, "reconnect", cap.SLOW_COMMAND);
  await cap.sleep(2000);
  await cap.waitInLevel(client.c, scaled(60000));
  await joinGame(client, sc);
  faults.push(...judgeStill(await moment(client.c, outDir, "reconnected"), "after its reconnect"));
  await cap.exec(sc, "map MAP01", cap.SLOW_COMMAND);
  await cap.sleep(3000);
  await cap.waitInLevel(client.c, scaled(60000));
  await cap.waitInLevel(sc, scaled(30000));
  faults.push(...judgeStill(await moment(client.c, outDir, "restarted"), "after the map restarted"));
  return faults.length
    ? { line: `FAIL ${label}: ${faults.join("; ")} (frames in ${outDir})`, failed: 1 }
    : { line: `PASS ${label}: the tic stream drew as the CPU path and carried every corpse on the glow, on joining, after a reconnect and after a map restart`, failed: 0 };
}

export const CONFIGS = [
  { name: "loopback", players: 1 },
  { name: "ping100", players: 1, lag: "100" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netgpusprites", wad: buildNetGpuSpritesWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
