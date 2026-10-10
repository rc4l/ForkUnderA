// [rc4l] A wall switch pressed with +use, against the same wall already showing its pressed texture: exit 0 when the bake picks up the switch's new texture.
//
// usage:  node scenarios/switchtex.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] The sector out of sight that Light_ChangeToValue, a special that always succeeds, re-lights so the switch always flips.
const DUMMY_TAG = 5;

function room(pressed) {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 200 }, (edge) => (edge === "north"
    ? { side: { texturemiddle: pressed ? "SW2BRCOM" : "SW1BRCOM" },
        line: pressed ? {} : { special: SPECIAL.Light_ChangeToValue, arg0: DUMMY_TAG, arg1: 200, playeruse: true } }
    : { side: { texturemiddle: "STARTAN2" } }));
  m.room(1000, 1000, 1064, 1064, { heightfloor: 0, heightceiling: 64, texturefloor: "FLAT1", textureceiling: "FLAT1",
    lightlevel: 100, id: DUMMY_TAG }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 128, y: 200, angle: 90, type: 1 });
  return m;
}

export function buildSwitchTexWad() {
  return wad([...mapLumps("MAP01", room(false)), ...mapLumps("MAP02", room(true))]);
}

// [rc4l] Standing at the switch, within use range, facing it.
export const VANTAGES = [{ name: "switch", at: { x: 128, y: 200, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The left switch panel, whose lights flip when it is pressed.
export const BOXES = { MAP01: { panel: { box: [0.0, 0.32, 0.18, 0.7], ceiling: 0.05 } } };

// [rc4l] Pressed by the player, against drawn pressed from the start.
export const SIDES = [{ name: "pressed", commands: ["+use", "-use"] }, { name: "drawn", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "switchtex", wad: buildSwitchTexWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
