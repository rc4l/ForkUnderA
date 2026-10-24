// [rc4l] A hosted map in a wad no client has: exit 0 when a client joining from the browser fetches the wad from the server itself, checks it and plays the server's map, alone and two at once.
//
// usage:  node scenarios/waddownload.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";
import { addClient } from "../src/netplay.mjs";
import { netdiff, diffLines } from "../src/netdiff.mjs";

const flats = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };

// [rc4l] A room unlike the IWAD's MAP01, with a dim corner and a clip, so a client on the wrong map differs from the server at once.
export function buildWadDownloadWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 768, 768, { ...flats, lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(512, 512, 704, 704, { ...flats, lightlevel: 112 }, room, () => ({}), () => ({ texturemiddle: "STARTAN2" }));
  for (const [i, y] of [[1, 128], [2, 256], [3, 384], [4, 512]]) m.thing({ x: 128, y, angle: 0, type: i });
  m.thing({ x: 384, y: 384, type: THING.Clip });
  return wad(mapLumps("MAP01", m));
}

const md5 = (buf) => crypto.createHash("md5").update(buf).digest("hex");

// [rc4l] The verdict from each downloader: whether it got in, the copy in its download folder against the server's, and its netdiff.
export function judgeDownloads(label, name, rows) {
  const faults = [];
  for (const r of rows) {
    if (r.error) faults.push(`${r.who} never got in: ${r.error}`);
    else if (!r.md5) faults.push(`${r.who} plays without ${name} in its download folder`);
    else if (r.md5 !== r.wantMd5) faults.push(`${r.who}'s ${name} is not the server's (md5 ${r.md5}, not ${r.wantMd5})`);
    if (r.diffs?.length) faults.push(`${r.who}: ${diffLines("", r.diffs, r.compared).slice(1).join(";").trim()}`);
  }
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: ${rows.length} client(s) fetched ${name} from the server and play its map as the server has it`, failed: 0 };
}

export async function judgeNetplay(set, label, { wadPath, config }) {
  const name = path.basename(wadPath), wantMd5 = md5(fs.readFileSync(wadPath));
  // [rc4l] One folder per client, emptied first, so an earlier run's copy cannot satisfy the join.
  const dirs = Array.from({ length: config.downloaders }, (_, i) => path.join(os.tmpdir(), `fua-waddownload-${i + 1}`));
  for (const d of dirs) { fs.rmSync(d, { recursive: true, force: true }); fs.mkdirSync(d, { recursive: true }); }
  // [rc4l] No mirrors, so the server's own endpoint is the only place the wad can come from.
  const joins = await Promise.allSettled(dirs.map((dir) => addClient(set, { files: [], via: "browser",
    cvars: { ...(config.clientCvars || {}), cl_fua_download_dir: dir.replace(/\\/g, "/"), cl_fua_downloadsites: "" } })));
  const rows = [];
  for (let i = 0; i < dirs.length; i++) {
    const file = path.join(dirs[i], name);
    const row = { who: `client ${i + 1}`, wantMd5, error: joins[i].status === "rejected" ? joins[i].reason.message : null,
      md5: fs.existsSync(file) ? md5(fs.readFileSync(file)) : null, diffs: [], compared: {} };
    if (!row.error) Object.assign(row, await netdiff(set.server.c, joins[i].value.c, { seconds: 1 }));
    rows.push(row);
  }
  return judgeDownloads(label, name, rows);
}

// [rc4l] One client, then two at once; --side one --cvar sv_fua_download=0 shows a server that serves nothing failing it.
export const CONFIGS = [
  { name: "one", players: 0, downloaders: 1 },
  { name: "two-at-once", players: 0, downloaders: 2 },
];

runScenarioIfMain(import.meta.url, () => ({ name: "waddownload", wad: buildWadDownloadWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
