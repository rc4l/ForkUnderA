// [rc4l] lighttable's rooms lit from the sector light table, their light changing every tic: exit 0 when the changes reach the GPU as table entries and next to no surface record is sent again, which re-sent dozens a frame before the table.
//
// usage:  node scenarios/lighttablesends.mjs [port]
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { parseConsoleJson } from "../src/telemetry.mjs";
import { buildLightTableWad, VANTAGES } from "./lighttable.mjs";

// [rc4l] Surface records re-sent a frame that still pass: the table leaves none on these rooms, and a stray rebake is a fraction of one.
export const SENT_CEILING = 0.5;

// [rc4l] The verdict from a frame's mean surfaces repainted for their light (shading) and records sent (sent): a light that changed, and the table carrying it.
export function judgeLightTableSends(label, { shading, sent, tics }) {
  if (!(tics > 0)) return { line: `FAIL ${label}: no tic ran while it waited, so nothing could change`, failed: 1 };
  if (!(shading >= 1)) return { line: `FAIL ${label}: ${shading} surfaces a frame repainted for their light, so the rooms' light did not change and the ceiling proves nothing`, failed: 1 };
  if (!(sent <= SENT_CEILING)) return { line: `FAIL ${label}: ${sent.toFixed(1)} surface records sent a frame for ${shading.toFixed(1)} light changes, which the sector light table should carry`, failed: 1 };
  return { line: `PASS ${label}: ${shading.toFixed(1)} light changes a frame, ${sent.toFixed(2)} surface records sent`, failed: 0 };
}

// [rc4l] One counter's mean per frame from fua_stats json, 0 for one never counted.
export function meanOf(rows, name) {
  const row = rows.find((r) => r.name === name);
  return row ? Number(row.mean) : 0;
}

export async function judgeLaunch(c, inst, label) {
  // [rc4l] In the glow room, whose light steps every tic, a frame a tic, for long enough to fill the stats window's 240 frames.
  await c.rpc("player.setpos", VANTAGES.find((v) => v.name === "glow").at);
  await cap.exec(c, "vid_maxfps 35");
  const from = cap.ticOf(await c.rpc("sim.tic"));
  await cap.sleep(8000);
  const tics = cap.ticOf(await c.rpc("sim.tic")) - from;
  const rows = [...parseConsoleJson(await cap.exec(c, "fua_stats json levelmesh/repaint_shading", cap.SLOW_COMMAND)),
    ...parseConsoleJson(await cap.exec(c, "fua_stats json gpu/repaint_sent", cap.SLOW_COMMAND))];
  return judgeLightTableSends(label, { shading: meanOf(rows, "levelmesh/repaint_shading"), sent: meanOf(rows, "gpu/repaint_sent"), tics });
}

// [rc4l] Vulkan alone, as players run it; --side vk --cvar fua_dg_sectorlit=0 shows the records sent again.
runScenarioIfMain(import.meta.url, () => ({ name: "lighttablesends", wad: buildLightTableWad(), maps: ["MAP02"], sides: [{ name: "vk" }], judgeLaunch }), compareEachLaunch);
