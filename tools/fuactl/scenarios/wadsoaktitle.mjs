// [rc4l] The WAD set swapped again and again to the title screen, where no level loads, with the engine counting its memory after each: exit 0 when none keeps growing.
//
// usage:  node scenarios/wadsoaktitle.mjs [port]
import { runScenarioIfMain, compareSoak } from "../src/template.mjs";
import { buildSoakWad } from "./soak.mjs";
import { SIDES, wadSwapSteps } from "./wadsoak.mjs";

// [rc4l] The backend kept its textures for every material a restart freed until a level loaded, so a restart to the title grew GPU memory and could hand a new material an old one's picture.
runScenarioIfMain(import.meta.url, () => ({ name: "wadsoaktitle", wad: buildSoakWad(), maps: ["MAP01"], sides: SIDES, cycles: 6, warmup: 2, action: (files) => wadSwapSteps(files) }), compareSoak);
