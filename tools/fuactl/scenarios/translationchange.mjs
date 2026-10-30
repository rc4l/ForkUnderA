// [Doruk-Aksoy] An actor whose ACS translation is redefined after it was drawn, and the player seen in chasecam after a colour change, each against the same colours from the level's start: exit 0 when Vulkan, with the material array and with a binding per material, draws the new colours.
//
// usage:  node scenarios/translationchange.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { acsScripts, callSpecial, createTranslation, PCD } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

const PILLAR_TID = 7;
// [Doruk-Aksoy] Every palette index onto the greens, then onto the reds, so the pillar's whole sprite changes colour.
export const GREEN = [[0, 255, 112, 127]];
export const RED = [[0, 255, 176, 191]];

// [Doruk-Aksoy] A lit hall with a tech pillar in it, recoloured by ACS translation 1 from the level's start (`first`), and closed script 2 redefining that translation as `then`.
export function hall(first, then) {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 },
    () => ({ side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  m.thing({ x: 512, y: 640, angle: 270, type: THING.TechPillar, id: PILLAR_TID });
  const behavior = acsScripts([
    { code: [...createTranslation(1, first), ...callSpecial(SPECIAL.Thing_SetTranslation, [PILLAR_TID, 1]), PCD.TERMINATE] },
    { code: [...createTranslation(1, then), PCD.TERMINATE], open: false, number: 2 },
  ]);
  return { m, behavior };
}

// [Doruk-Aksoy] MAP01 starts green and is recoloured red by `puke 2`; MAP02 is red from its start, which is what the recoloured pillar must look like.
export function buildTranslationChangeWad() {
  const a = hall(GREEN, RED), b = hall(RED, RED);
  return wad([...mapLumps("MAP01", a.m, { behavior: a.behavior }), ...mapLumps("MAP02", b.m, { behavior: b.behavior })]);
}

// [Doruk-Aksoy] Close in front of the pillar, so its sprite fills the middle of the frame.
const AT = { x: 512, y: 480, z: 0, angle: 90, pitch: 0 };
export const VANTAGES = [{ name: "pillar", at: AT }];

// [Doruk-Aksoy] The pillar drawn green first, so its slot and binding are made before the recolour, under each way the backend binds materials.
const recolour = (bindless) => [
  { name: `${bindless}_green`, commands: [`fua_dg_bindless ${bindless === "array" ? 1 : 0}`, "map MAP01", 3000, AT, 1500] },
  { name: `${bindless}_recoloured`, commands: ["puke 2", 1500] },
  { name: `${bindless}_oracle`, commands: ["map MAP02", 3000, AT, 1500] },
];
// [Doruk-Aksoy] The player's own colours, rebuilt under the same translation number by a colour change, seen in chasecam facing away from the pillar.
const BEHIND = { x: 512, y: 300, z: 0, angle: 0, pitch: 0 };
const PLAYER_STEPS = [
  { name: "player_green", commands: ["fua_dg_bindless 1", 'color "00 ff 00"', "map MAP02", 3000, BEHIND, "chase", 1500] },
  { name: "player_recoloured", commands: ['color "ff 00 00"', 1500] },
  // [Doruk-Aksoy] `map` starts a new game, which turns chasecam off again.
  { name: "player_oracle", commands: ["map MAP02", 3000, BEHIND, "chase", 1500] },
];
export const STEPS = [...recolour("array"), ...recolour("binding"), ...PLAYER_STEPS];

const PILLAR = [0.42, 0.25, 0.58, 0.75];
// [Doruk-Aksoy] Green against red proves the recolour is there to see; the recoloured pillar against the oracle is the fix.
const judged = (bindless) => ({
  [`${bindless}_changes`]: { box: PILLAR, pair: [`${bindless}_green`, `${bindless}_oracle`], floor: 10 },
  [`${bindless}_follows`]: { box: PILLAR, pair: [`${bindless}_recoloured`, `${bindless}_oracle`], ceiling: 0.5 },
});
const PLAYER = [0.44, 0.45, 0.56, 0.8];
export const BOXES = { MAP01: { ...judged("array"), ...judged("binding"),
  player_changes: { box: PLAYER, pair: ["player_green", "player_oracle"], floor: 10 },
  player_follows: { box: PLAYER, pair: ["player_recoloured", "player_oracle"], ceiling: 0.5 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "translationchange", wad: buildTranslationChangeWad(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);
