// [rc4l] cl_netgraph (zandronum@9a8ae9d825e8): exit 0 when a client showing stat nettraffic with the net graph on draws its In and Out lines, their legend and a line that moves off the bottom, above the stat text at the screen's lower left.
//
// usage:  node scenarios/netgraph.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { captureVulkan } from "../src/shot.mjs";
import { decodePNG, px } from "../src/pngcore.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] A grey room, so nothing but the graph's lines is the In line's red or the Out line's green.
const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_4", textureceiling: "FLAT5_4", lightlevel: 160 };

export function buildNetGraphPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "GRAY1" } }));
  m.thing({ x: 128, y: 64, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] The In and Out lines' colours as cl_statistics.cpp draws them.
export const LINE_COLORS = { In: [255, 20, 35], Out: [100, 255, 55] };
// [rc4l] Both graphs fit 480 pixels wide and 360 tall at the lower left, above the stat text's green rows at the bottom.
export const GRAPH_REGION = { w: 480, h: 360, textRows: 40 };
// [rc4l] Seconds of traffic before the capture, so the newest seconds stand off the bottom.
export const TRAFFIC_TICS = 5 * 35;

const near = (a, b) => a.every((v, i) => Math.abs(v - b[i]) <= 40);

// [rc4l] Each line colour's pixels in the region and the rows they cover, from px(x, y) over a w by h image.
export function lineColorRows(pixelAt, w, h, region = GRAPH_REGION) {
  const out = Object.fromEntries(Object.keys(LINE_COLORS).map((k) => [k, { count: 0, rows: new Set() }]));
  for (let y = Math.max(0, h - region.h); y < h - region.textRows; y++) {
    for (let x = 0; x < Math.min(w, region.w); x++) {
      const p = pixelAt(x, y);
      for (const [k, c] of Object.entries(LINE_COLORS)) if (near(p, c)) { out[k].count++; out[k].rows.add(y); }
    }
  }
  return Object.fromEntries(Object.entries(out).map(([k, v]) => [k, { count: v.count, rows: v.rows.size }]));
}

// [rc4l] What is wrong with the colours found: each line's legend is 16 pixels on one row, so a drawn line has more and the In line more than one row.
export function judgeLineColors(found) {
  const faults = [];
  for (const k of Object.keys(LINE_COLORS)) if (!(found[k]?.count >= 16)) faults.push(`no ${k} line or legend (${found[k]?.count ?? 0} pixels)`);
  if (!faults.length && !(found.In.rows >= 3)) faults.push(`the In line lies on ${found.In.rows} row(s), never off the bottom`);
  return faults;
}

export async function judgeNetplay(set, label) {
  const client = set.clients.find((c) => c.role === "player");
  await cap.exec(client.c, "stat nettraffic");
  await cap.waitTics(client.c, TRAFFIC_TICS);
  const dir = path.join(os.tmpdir(), "fuactl-netgraph");
  fs.mkdirSync(dir, { recursive: true });
  const file = await captureVulkan(client.c, path.join(dir, `${label.replace(/\W+/g, "_")}.png`).split(path.sep).join("/"));
  if (!file) return { lines: [`FAIL ${label}: the client's frame never captured`], failed: 1 };
  const img = decodePNG(file);
  const found = lineColorRows((x, y) => px(img, x, y), img.w, img.h);
  const faults = judgeLineColors(found);
  const facts = Object.entries(found).map(([k, v]) => `${k} ${v.count} px on ${v.rows} rows`).join(", ");
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")} (${file})` : `PASS ${label}: the net graph drew ${facts}`], failed: faults.length ? 1 : 0 };
}

// [rc4l] screenblocks 12 takes the status bar's red numbers off the graph's corner.
export const CONFIGS = [{ name: "graph", players: 1, clientCvars: { cl_netgraph: 1, screenblocks: 12 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netgraph", wad: buildNetGraphPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
