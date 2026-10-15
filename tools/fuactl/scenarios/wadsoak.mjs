// [rc4l] The WAD set swapped again and again in one launch with wad_reload, which restarts the engine in place, with the engine counting its memory, objects, handles and threads after each return: exit 0 when none keeps growing.
//
// usage:  node scenarios/wadsoak.mjs [port]
import path from "node:path";
import { runScenarioIfMain, compareSoak, FREEDOOM2 } from "../src/template.mjs";
import { buildSoakWad } from "./soak.mjs";

// [rc4l] Every restart leaked what startup made and the restart never freed: actor light lists, the Huffman codec, the client's net buffer, weapon order entries, the startup screen, and with music muted a WAD file handle a level.
export const FREEDOOM1 = path.join(path.dirname(FREEDOOM2), "freedoom1.wad");

export const SIDES = [{ name: "vk", noGl: true }];

// [rc4l] The two reloads of a cycle: over to the other IWAD alone and back to the launch's own set, each into map when one is given; the console reads a backslash as an escape, so paths go forward.
export function wadSwapSteps({ wadPath, iwad }, maps = []) {
  const slash = (p) => p.split(path.sep).join("/");
  const at = (i) => (maps[i] ? ` map=${maps[i]}` : "");
  return `wad_reload ${slash(FREEDOOM1)}${at(0)}|wad_reload ${slash(iwad)} ${slash(wadPath)}${at(1)}`;
}

// [rc4l] Each cycle loads and unloads an IWAD, a PWAD and two maps.
export const wadSwap = (files) => wadSwapSteps(files, ["E1M1", "MAP01"]);

// [rc4l] Private bytes settle over the first sixty or so restarts, the heap's commit and the driver's caches, and level off near 800 MB (measured over 80), so live allocations, heap bytes, the GPU and handles judge the leak instead.
export const IGNORE = ["mem/private_bytes"];

runScenarioIfMain(import.meta.url, () => ({ name: "wadsoak", wad: buildSoakWad(), maps: ["MAP01"], sides: SIDES, cycles: 6, warmup: 2, action: wadSwap, ignore: IGNORE }), compareSoak);
