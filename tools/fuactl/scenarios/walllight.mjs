// [rc4l] A wall lit by Transfer_WallLight from a sector a script brightens after the level starts, against the same wall lit 255 from the start: exit 0 when Vulkan shows the transferred light as it shows the fixed one.
//
// usage:  node scenarios/walllight.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

const LINE_ID = 1, SOURCE_TAG = 5;

// [rc4l] A dark room whose north wall takes its light from northWall(): the transfer's target on MAP01, a fixed absolute light on MAP02.
function lightRoom(northWall) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 64 }, (edge) => (edge === "north" ? northWall : { side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return m;
}

// [rc4l] WDI09's light panels take their light from a sector its scripts fade, and both GL's wall cache and the bake kept the first light they saw.
export function buildWallLightWad() {
  const transfer = lightRoom({ side: { texturemiddle: "STARTAN2" }, line: { id: LINE_ID } });
  // [rc4l] The source, out of sight: its west wall carries the transfer to the north wall's front side.
  transfer.room(1024, 0, 1152, 128, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 96, id: SOURCE_TAG }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Transfer_WallLight, arg0: LINE_ID, arg1: 1 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] Half a second in, after the level is baked, the source goes to full light.
  const behavior = acs0([PCD.DELAYDIRECT, 18, ...callSpecial(SPECIAL.Light_ChangeToValue, [SOURCE_TAG, 255]), PCD.TERMINATE]);
  const fixed = lightRoom({ side: { texturemiddle: "STARTAN2", light: 255, lightabsolute: true } });
  return wad([...mapLumps("MAP01", transfer, { behavior }), ...mapLumps("MAP02", fixed)]);
}

// [rc4l] Vulkan on both: GL's own wall cache had the same fault, so only a map lit right from the start is a fair reference.
export const SIDES = [{ name: "transfer" }, { name: "fixed", commands: ["map MAP02"] }];

// [rc4l] Facing the lit wall from the room's south half.
export const VANTAGES = [{ name: "wall", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The wall's face, above the weapon.
export const BOXES = { MAP01: { wall: { box: [0.1, 0.15, 0.9, 0.6], ceiling: 2 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "walllight", wad: buildWallLightWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
