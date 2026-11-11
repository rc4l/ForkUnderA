// [Doruk-Aksoy] A Bright sprite, a Bright model and a plain sprite in a dark room's coloured fog, and in black fog, GL against Vulkan: exit 0 when Vulkan fogs each at its sector's light as GL, Zandronum 3.3 and UZDoom do (GLSprite::Draw's foglevel), the sprite's own light under black fog.
//
// usage:  node scenarios/brightfog.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [Doruk-Aksoy] A square of one colour, a sprite's grabbed at its bottom middle so it stands on its actor.
const solid = (rgb, n = 64, grab = null) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab }); };

// [Doruk-Aksoy] Two Bright actors (a sprite and a cube model) and a plain sprite beside them, all orange.
const ACTORS = [
  { name: "ZXBrightSprite", num: 20130, sprite: "ZXBS", bright: " Bright", x: 160 },
  { name: "ZXBrightModel", num: 20131, sprite: "ZXBM", bright: " Bright", x: 256 },
  { name: "ZXPlainSprite", num: 20132, sprite: "ZXPS", bright: "", x: 352 },
];

function decorate() {
  return ACTORS.map((a) => `Actor ${a.name} ${a.num}\n{\n  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ${a.sprite} A -1${a.bright}\n    Stop\n  }\n}\n`).join("\n");
}

const MODELDEF = `Model ZXBrightModel\n{\n  Path "models/"\n  Model 0 "cube.kvx"\n  Path "graphics/"\n  Skin 0 "ZXSKIN.png"\n  Scale 4.0 4.0 4.0\n  FrameIndex ZXBM A 0 0\n}\n`;

// [Doruk-Aksoy] A dark room, MAP01 in a blue coloured fog and MAP02 in black fog, the actors in a row ahead of the player.
function room(fade) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 64,
    ...(fade ? { fadecolor: fade } : {}) }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const a of ACTORS) m.thing({ x: a.x, y: 320, angle: 270, type: a.num });
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return m;
}

export function buildBrightFogPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room(0x203060))) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room(0))) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: MODELDEF },
    { name: "models/cube.kvx", data: kvxBox(8, 8, 8) },
    { name: "graphics/ZXSKIN.png", data: solid([255, 128, 0], 16) },
    // [Doruk-Aksoy] The model's frame needs a sprite to exist, though the model is what draws.
    ...["ZXBSA0", "ZXBMA0", "ZXPSA0"].map((s) => ({ name: `sprites/${s}.png`, data: solid([255, 128, 0], 64, [32, 64]) })),
  ]);
}

export const VANTAGES = [{ name: "row", at: { x: 256, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] Each actor's middle, left to right.
const AREAS = { brightSprite: [0.335, 0.47, 0.385, 0.58], brightModel: [0.48, 0.54, 0.52, 0.62], plainSprite: [0.615, 0.47, 0.665, 0.58] };
export const BOXES = Object.fromEntries(["MAP01", "MAP02"].map((map) =>
  [map, Object.fromEntries(Object.entries(AREAS).map(([n, box]) => [n, { box, ceiling: 3 }]))]));

runScenarioIfMain(import.meta.url, () => ({ name: "brightfog", wad: buildBrightFogPk3(), maps: ["MAP01", "MAP02"],
  vantages: VANTAGES, boxes: BOXES }));
