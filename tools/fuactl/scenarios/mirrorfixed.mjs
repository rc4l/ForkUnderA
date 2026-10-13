// [rc4l] A line mirror under invulnerability, GL against Vulkan: exit 0 when Vulkan recolours the reflection and its sheen as GL's main.fp does under a fixed colormap.
//
// usage:  node scenarios/mirrorfixed.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildLineMirrorWad, VANTAGES, BOXES } from "./linemirror.mjs";
import { fixedColormapSides } from "./fixedcolormap.mjs";

runScenarioIfMain(import.meta.url, () => ({ name: "mirrorfixed", wad: buildLineMirrorWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: fixedColormapSides("InvulnerabilitySphere") }));
