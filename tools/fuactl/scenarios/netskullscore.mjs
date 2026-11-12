// [rc4l] A server's sv_requireskulltoscore reaches its clients (zandronum@f5b68175b468): exit 0 when a client joins holding the server's 0 and follows it to 1 when the server changes it.
//
// usage:  node scenarios/netskullscore.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { cvarValueFrom } from "../src/shot.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const CVAR = "sv_requireskulltoscore";

export function buildNetSkullScorePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] What is wrong with a client's echo of the cvar against the server's 0 or 1, `when` naming the moment.
export function judgeEcho(echo, want, when) {
  const got = cvarValueFrom(echo);
  // [rc4l] A Bool cvar echoes true or false.
  return got !== null && Number(got === "true" || got === "1") === Number(want) ? [] : [`${when} the client's ${CVAR} is ${got ?? `unknown (${String(echo).trim().slice(0, 80)})`}, not the server's ${want}`];
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c, cc = set.clients[0].c;
  const faults = judgeEcho(await cap.exec(cc, CVAR), 0, "joined,");
  await cap.exec(sc, `${CVAR} 1`);
  await cap.waitTics(sc, 35);
  faults.push(...judgeEcho(await cap.exec(cc, CVAR), 1, "changed,"));
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the client held ${CVAR} 0 from the server and followed it to 1`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "lan", players: 1, serverCvars: { [CVAR]: 0 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netskullscore", wad: buildNetSkullScorePk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
