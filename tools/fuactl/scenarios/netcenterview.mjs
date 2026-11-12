// [rc4l] centerview levels a client's view under ping (zandronum@e8b7e77c2fe6): exit 0 when a player looking down looks level after centerview, with and without lag.
//
// usage:  node scenarios/netcenterview.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] How far down the player looks first, and how near level centerview must bring it.
export const LOOK_DOWN = 30, LEVEL_WITHIN = 1;

export function buildNetCenterViewPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] What is wrong with the pitches read before and after centerview, in degrees, down positive.
export function judgePitches(before, after) {
  if (!(before >= LOOK_DOWN - 5)) return [`looked down only ${before?.toFixed?.(2)} degrees before centerview`];
  if (!(Math.abs(after) < LEVEL_WITHIN)) return [`still ${after?.toFixed?.(2)} degrees off level after centerview`];
  return [];
}

// [rc4l] Rounds of looking down and centering, since under loss a prediction ends the centering only some of the time.
export const ROUNDS = 6;

export async function judgeNetplay(set, label) {
  const player = set.clients.find((c) => c.role === "player");
  const faults = [];
  for (let round = 1; round <= ROUNDS; round++) {
    await player.c.rpc("input.look", { pitch: LOOK_DOWN });
    await cap.waitTics(player.c, 20);
    const before = (await player.c.rpc("player.camera", {}))?.pitch;
    await cap.exec(player.c, "centerview");
    await cap.waitTics(player.c, 35);
    const after = (await player.c.rpc("player.camera", {}))?.pitch;
    faults.push(...judgePitches(before, after).map((f) => `round ${round} ${f}`));
  }
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: looked down and back level ${ROUNDS} times`], failed: faults.length ? 1 : 0 };
}

// [rc4l] Under loss a prediction ran the centering, which an old build stopped short in 1 round of 14; at 40% loss the client loses its own looks on either build.
export const CONFIGS = [{ name: "ping150", players: 1, lag: "150" }, { name: "ping150loss20", players: 1, lag: "150", loss: "20" }];

runScenarioIfMain(import.meta.url, () => ({ name: "netcenterview", wad: buildNetCenterViewPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
