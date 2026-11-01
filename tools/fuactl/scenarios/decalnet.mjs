// [rc4l] features/decals' marks are a client's alone: a client joined to a hosted map fires at a wall and at the floor, its fua_decal_report has both made and alive, the server's has the same shots refused as a server's with none alive, and the client's render copy found every wall holding a mark.
//
// usage:  node scenarios/decalnet.mjs [port]
import { runScenarioIfMain, compareHosted } from "../src/template.mjs";
import { buildDecalRulesWad, parseDecalReport } from "./decalrules.mjs";
import { parsePrescanReport } from "../src/copycheck.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] The verdict: the client made a wall and a floor mark and holds them, the server refused both as a server's and holds none, and the client's fills, which list the walls holding marks from a log, missed none.
export function judgeDecalNet(label, { client, server, fills }) {
  const faults = [];
  if (fills && !fills.found) faults.push("the client gave no fua_copy_prescan_report");
  else if (fills && fills.differed > 0) faults.push(`the client's render copy missed ${fills.differed} changes, the first ${fills.first}`);
  if (!client || client.walls === null) faults.push("the client gave no fua_decal_report");
  else {
    if (!(client.counts["wall made"] > 0) || !(client.walls > 0)) faults.push("the client made no wall mark");
    if (!(client.counts["floor made"] > 0) || !(client.flats > 0)) faults.push("the client made no floor mark");
  }
  if (!server || server.walls === null) faults.push("the server gave no fua_decal_report");
  else {
    if (!(server.counts["wall server"] > 0) || !(server.counts["floor server"] > 0)) faults.push("the server did not refuse the shots as a server's");
    if (Object.keys(server.counts).some((k) => k.endsWith(" made")) || server.walls !== 0 || server.flats !== 0) {
      faults.push(`the server made marks (${server.walls} wall, ${server.flats} floor alive)`);
    }
  }
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the client made ${client.walls} wall and ${client.flats} floor marks, the server refused its shots and holds none`, failed: 0 };
}

// [rc4l] One shot from the client: fire held a few tics so a raised weapon catches it, then time for it to land on both.
async function fire(cc) {
  await cap.exec(cc, "+attack", { quietMs: 60 });
  await cap.waitTics(cc, 4);
  await cap.exec(cc, "-attack", { quietMs: 60 });
  await cap.waitTics(cc, 35);
}

const report = async (c) => parseDecalReport(await cap.exec(c, "fua_decal_report", { quietMs: 400 }));

export async function judgeHosted(sc, cc, label) {
  for (const c of [sc, cc]) await cap.exec(c, "fua_decal_report reset");
  await cap.exec(cc, "fua_copy_prescan_check 1");
  // [rc4l] At the north wall the start faces, then at the floor, since a netgame refuses setpos and only looks.
  await fire(cc);
  await cc.rpc("input.look", { pitch: 60 });
  await cap.waitTics(cc, 10);
  await fire(cc);
  const fills = parsePrescanReport(await cap.exec(cc, "fua_copy_prescan_report", { quietMs: 400 }));
  return judgeDecalNet(label, { client: await report(cc), server: await report(sc), fills });
}

runScenarioIfMain(import.meta.url, () => ({ name: "decalnet", wad: buildDecalRulesWad().wad, maps: ["MAP01"], judgeHosted }), compareHosted);
