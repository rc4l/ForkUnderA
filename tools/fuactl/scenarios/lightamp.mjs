// [rc4l] Light amplification's night-vision green, GL against Vulkan: exit 0 when Vulkan tints the world, the sky, a sprite and the weapon as GL's main.fp does, at full light in a dark alcove too.
//
// usage:  node scenarios/lightamp.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildFixedColormapWad, VANTAGES, BOXES, fixedColormapSides } from "./fixedcolormap.mjs";

runScenarioIfMain(import.meta.url, () => ({ name: "lightamp", wad: buildFixedColormapWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: fixedColormapSides("Infrared") }));
