// [Doruk-Aksoy] GLDEFS light parity with UZDoom: one small black map per light property, screenshotted in UZDoom and in ours,
// each lit spot's added brightness taken as a ratio of its map's reference spot; exit 0 when ours match UZDoom's.
// usage: node uzdoom/lightparity.mjs [--port 7875] [--only MAP02] [--api vulkan|opengl|both] [--uzdoom-backend gl|vulkan]   (ours is the build FUACTL_ENGINE or the staged one, on each API; UZDoom is FUACTL_UZDOOM or UZDoom-build beside the repo)
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { uzdoomShot, oursShot } from "./shots.mjs";
import { parityApis, parityUzdoomBackend } from "../src/uzdoom.mjs";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG, decodePNG, px } from "../src/pngcore.mjs";

const args = process.argv.slice(2);
const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : null);
const port = Number(flag("--port")) || 7875;
const apis = parityApis(args), uzBackend = parityUzdoomBackend(args);
const out = fs.mkdtempSync(path.join(os.tmpdir(), "lightparity-"));

// [Doruk-Aksoy] A black room (lightlevel 0), so an unlit pixel is near zero in both engines.
function blackRoom(w, h, ceiling) {
  const m = new UdmfMap();
  m.room(0, 0, w, h, { heightfloor: 0, heightceiling: ceiling, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 0 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "ZXGRAY" : "BROWN96" } }));
  return m;
}

// [Doruk-Aksoy] A DECORATE actor of one sprite frame, invisible or not, which a GLDEFS object block can hang a light on.
const actor = (name, num, sprite, invisible) =>
  `Actor ${name} ${num}\n{\n  Radius 4\n  Height 52\n  +NOGRAVITY\n  +NOBLOCKMAP\n${invisible ? "  +INVISIBLE\n" : ""}  States\n  {\n  Spawn:\n    ${sprite} A -1\n    Stop\n  }\n}\n`;
const light = (name, cls, frame, extra) =>
  `pointlight ${name}\n{\n  color 0.3 0.3 0.3\n  size 56\n${extra}}\nobject ${cls}\n{\n  frame ${frame} { light ${name} }\n}\n`;

// [Doruk-Aksoy] Each case: a map, what it adds to DECORATE and GLDEFS, the picture zones (x0, x1 as fractions) holding one lit spot each, and which zone is the reference.
const CASES = [
  {
    map: "MAP01", title: "intensity: three lights on a grey wall",
    labels: ["intensity 0.5", "intensity 1.0", "intensity 2.0"], reference: 1,
    zones: [[0.1, 0.43], [0.43, 0.57], [0.57, 0.9]], rows: [0.15, 0.62],
    build() {
      const m = blackRoom(1536, 1024, 128);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      const intensities = [0.5, 1.0, 2.0];
      intensities.forEach((_, i) => m.thing({ x: 512 + 256 * i, y: 1008, angle: 0, type: 20240 + i, height: 48 }));
      return {
        map: m,
        decorate: intensities.map((_, i) => actor(`ZXLightProbe${i}`, 20240 + i, "BON1", true)).join("\n"),
        gldefs: intensities.map((v, i) => light(`ZXPROBE${i}`, `ZXLightProbe${i}`, "BON1A", `  intensity ${v}\n`)).join("\n"),
      };
    },
  },
  {
    map: "MAP02", title: "dontlightactors: two floating sprites, each carrying a light",
    labels: ["plain light", "dontlightactors 1"], reference: 0,
    zones: [[0.05, 0.5], [0.5, 0.95]], rows: [0.1, 0.62],
    build() {
      // [Doruk-Aksoy] The sprites float 150 up under a 512 ceiling, beyond the lights' reach of floor, ceiling and walls, so only a sprite can take their light.
      const m = blackRoom(1536, 1024, 512);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      m.thing({ x: 512, y: 512, angle: 270, type: 20250, height: 150 });
      m.thing({ x: 1024, y: 512, angle: 270, type: 20251, height: 150 });
      return {
        map: m,
        decorate: [actor("ZXLitSprite0", 20250, "COL1", false), actor("ZXLitSprite1", 20251, "COL1", false)].join("\n"),
        gldefs: [light("ZXSPRITELIGHT0", "ZXLitSprite0", "COL1A", ""), light("ZXSPRITELIGHT1", "ZXLitSprite1", "COL1A", "  dontlightactors 1\n")].join("\n"),
      };
    },
  },
  {
    map: "MAP03", title: "dontlightothers: a floating sprite carrying a light, a bare one 64 units beside it",
    labels: ["carrier", "bare neighbour"], reference: 0,
    zones: [[0.2, 0.5], [0.5, 0.8]], rows: [0.1, 0.62],
    build() {
      const m = blackRoom(1536, 1024, 512);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      m.thing({ x: 736, y: 512, angle: 270, type: 20260, height: 150 });
      m.thing({ x: 800, y: 512, angle: 270, type: 20261, height: 150 });
      return {
        map: m,
        decorate: [actor("ZXOthersCarrier", 20260, "COL1", false), actor("ZXOthersBare", 20261, "COL2", false)].join("\n"),
        gldefs: light("ZXOTHERSLIGHT", "ZXOthersCarrier", "COL1A", "  dontlightothers 1\n"),
      };
    },
  },
  reachCase("MAP04", "size past 255: a size 100 and a size 450 light high on one tall wall, far off", 100, 450, 20270),
  reachCase("MAP05", "control inside 255: a size 40 and a size 180 light, the same 1:4.5", 40, 180, 20272),
  sectorCase("MAP06", "sector light scale 4/255 at light level 200: radius 200 against 100", 200, "0.017", 20274),
  sectorCase("MAP07", "sector light scale 1.0 at light level 64: radius 64 against 255", 64, "1.0", 20276),
  mapSectorCase("MAP08", "control: map-placed SectorPointLights (9803), args[3] 0 against 4, at light level 200", 200, 4),
  wallPairCase("MAP09", "attenuate: a plain and an attenuate 1 light, 48 in front of a wall", ["plain", "attenuate 1"], 20280,
    (probe) => ({ gldefs: [probe(0, ""), probe(1, "  attenuate 1\n")].join("\n") })),
  wallPairCase("MAP10", "MAPINFO attenuatelights: an attenuate 0 light and one without the tag", ["attenuate 0", "no tag"], 20282,
    (probe) => ({ gldefs: [probe(0, "  attenuate 0\n"), probe(1, "")].join("\n"), mapinfo: `map MAP10 "Attenuate"\n{\n  attenuatelights\n}\n` })),
  wallPairCase("MAP11", "map things: a PointLight (9800) and a PointLightAttenuated (9830)", ["9800", "9830"], 0,
    () => ({ things: [9800, 9830] })),
  spotPairCase("MAP13", "GLDEFS spot 10 25 facing the wall, against a plain light", ["plain", "spot facing the wall"], 20286,
    (probe) => ({ gldefs: [probe(0, ""), probe(1, "  spot 10 25\n")].join("\n"), angles: [90, 90] })),
  spotPairCase("MAP14", "GLDEFS spot 10 25 facing away from the wall, against a plain light", ["plain", "spot facing away"], 20288,
    (probe) => ({ gldefs: [probe(0, ""), probe(1, "  spot 10 25\n")].join("\n"), angles: [90, 270] })),
  spotPairCase("MAP16", "map things: SpotLight 9840 facing the wall, against a PointLight 9800", ["9800", "9840"], 0,
    () => ({ things: [{ type: 9800, arg0: 255, arg1: 255, arg2: 255, arg3: 96 }, spotThing(0, 0, 0, 90, 96, 0, 0xffffff)] })),
  spotPairCase("MAP17", "map things: SpotLight 9840 pitched 45 down, against one level", ["9840 level", "9840 pitched 45 down"], 0,
    () => ({ things: [spotThing(0, 0, 0, 90, 96, 0, 0xffffff), spotThing(0, 0, 0, 90, 96, 45, 0xffffff)] })),
  spotSpriteCase("MAP15", "sprites: one lit by a plain light, one by a spot aimed at it, one by a spot aimed away"),
  floorLightCase("MAP18", "attenuated lights on actors lying on the floor: one lifted 5, one +NOINTERACTION left there"),
  spotPairCase("MAP19", "map things: a PointLight red by arg0 255 0 0, and one by UDMF arg0str \"red\"", ["args 255 0 0", "arg0str red"], 0,
    () => ({ things: [{ type: 9800, arg0: 255, arg1: 0, arg2: 0, arg3: 96 }, { type: 9800, arg0str: "red", arg3: 96 }] })),
  alphaCase("MAP20", "carriers at alpha 0.25: one plain, one +LIGHTMULTALPHA"),
  // [Doruk-Aksoy] UZDoom adds an additive light after the texture, at a fifth: on the 0.5 grey wall it reads 0.2 / 0.5 of a plain light, not 0.2.
  wallPairCase("MAP21", "additive: a plain white light and an additive 1 one, 48 in front of a grey wall", ["plain", "additive 1"], 20300,
    (probe) => ({ gldefs: [probe(0, ""), probe(1, "  additive 1\n")].map((t) => t.replace("color 0.3 0.3 0.3", "color 1 1 1")).join("\n") })),
  subtractiveColourCase("MAP22", "subtractive colour: a white and a red subtractive light, 48 in front of a lit grey wall", 20302),
  // [Doruk-Aksoy] Last, since lightsizefactor holds for every definition after it in the lump.
  {
    ...reachCase("MAP12", "lightsizefactor 2: a size 40 light with attenuate 0 and one without the tag", 40, 40, 20284),
    labels: ["attenuate 0", "no tag"],
    gldefsPrefix: "lightsizefactor 2.0\n",
    gldefsExtra: ["  attenuate 0\n", ""],
  },
];

// [Doruk-Aksoy] A map spot light's thing: arg0 its colour 0xRRGGBB (0.3 grey), arg1 and arg2 its cone, arg3 its size.
const spotThing = (x, y, z, angle, size, pitch = 0, colour = 0x4d4d4d) => ({ x, y, angle, type: 9840, height: z, arg0: colour, arg1: 10, arg2: 25, arg3: size, ...(pitch ? { pitch } : {}) });

// [Doruk-Aksoy] Two white size 96 lights, bright enough that the dim tails both engines draw differently weigh little, 128 in front of the far wall of a 256-high black room, 96 up, at x 512 and 1024 either side of the view; extra(probe) gives GLDEFS and the probes' angles, or two map things placed there.
function spotPairCase(map, title, labels, num, extra) {
  return {
    map, title, labels, reference: 0, measure: "reach",
    zones: [[0.05, 0.5], [0.5, 0.95]], rows: [0.1, 0.62],
    build() {
      const m = blackRoom(1536, 1024, 256);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      const probe = (i, more) => light(`ZXSPOT${num + i}`, `ZXSpotProbe${num + i}`, "BON1A", more).replace("size 56", "size 96").replace("color 0.3 0.3 0.3", "color 1 1 1");
      const e = extra(probe);
      [512, 1024].forEach((x, i) => m.thing(e.things
        ? { angle: 90, ...e.things[i], x, y: 896, height: 96 }
        : { x, y: 896, angle: e.angles[i], type: num + i, height: 96 }));
      return {
        map: m,
        decorate: e.things ? "" : [0, 1].map((i) => actor(`ZXSpotProbe${num + i}`, num + i, "BON1", true)).join("\n"),
        gldefs: e.gldefs || "",
      };
    },
  };
}

// [Doruk-Aksoy] An invisible probe of one frame with DECORATE lines of its own.
const probeActor = (name, num, lines) =>
  `Actor ${name} ${num}\n{\n  Radius 4\n  Height 16\n  +NOGRAVITY\n  +NOBLOCKMAP\n  +INVISIBLE\n${lines}  States\n  {\n  Spawn:\n    BON1 A -1\n    Stop\n  }\n}\n`;

// [Doruk-Aksoy] Two probes lying on the floor 236 ahead, each carrying a white attenuate 1 light, the second +NOINTERACTION; the floor round each is measured.
function floorLightCase(map, title) {
  return {
    map, title, reference: 0, labels: ["lifted 5", "+NOINTERACTION"],
    zones: [[0.05, 0.5], [0.5, 0.95]], rows: [0.5, 0.78],
    build() {
      const m = blackRoom(1536, 1024, 128);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      m.thing({ x: 512, y: 300, angle: 90, type: 20292 });
      m.thing({ x: 1024, y: 300, angle: 90, type: 20293 });
      const lit = (i) => light(`ZXFLOOR${i}`, `ZXFloorProbe${i}`, "BON1A", "  attenuate 1\n").replace("size 56", "size 64").replace("color 0.3 0.3 0.3", "color 1 1 1");
      return {
        map: m,
        decorate: [probeActor("ZXFloorProbe0", 20292, ""), probeActor("ZXFloorProbe1", 20293, "  +NOINTERACTION\n")].join("\n"),
        gldefs: [lit(0), lit(1)].join("\n"),
      };
    },
  };
}

// [Doruk-Aksoy] A spot pair's geometry with two translucent (0.25) carriers of plain white lights, the second +LIGHTMULTALPHA.
function alphaCase(map, title) {
  const c = spotPairCase(map, title, ["plain carrier", "+LIGHTMULTALPHA"], 20294,
    (probe) => ({ gldefs: [probe(0, ""), probe(1, "")].join("\n"), angles: [90, 90] }));
  const build = c.build;
  c.build = () => {
    const b = build();
    b.decorate = [probeActor("ZXSpotProbe20294", 20294, "  RenderStyle Translucent\n  Alpha 0.25\n"),
      probeActor("ZXSpotProbe20295", 20295, "  RenderStyle Translucent\n  Alpha 0.25\n  +LIGHTMULTALPHA\n")].join("\n");
    return b;
  };
  return c;
}

// [Doruk-Aksoy] Three floating sprites 176 up under a 512 ceiling, each with a size 56 light 48 south of its middle: a PointLight, a spot facing it and a spot facing away, out of reach of floor, ceiling and walls.
function spotSpriteCase(map, title) {
  return {
    map, title, reference: 0,
    labels: ["plain light", "spot aimed at it", "spot aimed away"],
    zones: [[0.05, 0.36], [0.36, 0.64], [0.64, 0.95]], rows: [0.1, 0.62],
    build() {
      const m = blackRoom(1536, 1024, 512);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      [384, 768, 1152].forEach((x) => m.thing({ x, y: 560, angle: 270, type: 20290, height: 150 }));
      m.thing({ x: 384, y: 512, angle: 90, type: 9800, height: 176, arg0: 77, arg1: 77, arg2: 77, arg3: 56 });
      m.thing(spotThing(768, 512, 176, 90, 56));
      m.thing(spotThing(1152, 512, 176, 270, 56));
      return { map: m, decorate: actor("ZXSpotSprite", 20290, "COL1", false), gldefs: "" };
    },
  };
}

// [Doruk-Aksoy] Two lights 48 in front of the far wall of a 128-high black room, at x 512 and 1024; extra(probe) gives their GLDEFS (and MAPINFO), or map thing types in place of probe actors.
function wallPairCase(map, title, labels, num, extra) {
  return {
    map, title, labels, reference: 0, measure: "reach",
    zones: [[0.05, 0.5], [0.5, 0.95]], rows: [0.15, 0.62],
    build() {
      const m = blackRoom(1536, 1024, 128);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      const probe = (i, more) => light(`ZXWALL${num + i}`, `ZXWallProbe${num + i}`, "BON1A", more).replace("size 56", "size 64");
      const e = extra(probe);
      [512, 1024].forEach((x, i) => m.thing(e.things
        ? { x, y: 976, angle: 0, type: e.things[i], height: 48, arg0: 77, arg1: 77, arg2: 77, arg3: 64 }
        : { x, y: 976, angle: 0, type: num + i, height: 48 }));
      return {
        map: m,
        decorate: e.things ? "" : [0, 1].map((i) => actor(`ZXWallProbe${num + i}`, num + i, "BON1", true)).join("\n"),
        gldefs: e.gldefs || "",
        mapinfo: e.mapinfo || "",
      };
    },
  };
}

// [Doruk-Aksoy] A full-bright room grey all over, so the only dark pixels are what a subtractive light takes away.
function greyRoom(w, h, ceiling) {
  const m = new UdmfMap();
  m.room(0, 0, w, h, { heightfloor: 0, heightceiling: ceiling, texturefloor: "ZXGRAY", textureceiling: "ZXGRAY", lightlevel: 255 },
    () => ({ side: { texturemiddle: "ZXGRAY" } }));
  return m;
}

// [Doruk-Aksoy] UZDoom subtracts a subtractive light's |colour| - colour (hw_dynlightdata.cpp), so a red one darkens green and blue, nine tenths of a white one's dip; taking the colour itself darkens red alone, a third.
function subtractiveColourCase(map, title, num) {
  return {
    map, title, labels: ["white subtractive", "red subtractive"], reference: 0, measure: "dip",
    zones: [[0.05, 0.5], [0.5, 0.95]], rows: [0.15, 0.62],
    build() {
      const m = greyRoom(1536, 1024, 128);
      m.thing({ x: 768, y: 64, angle: 90, type: 1 });
      [512, 1024].forEach((x, i) => m.thing({ x, y: 976, angle: 0, type: num + i, height: 48 }));
      const colours = ["1 1 1", "1 0 0"];
      return {
        map: m,
        decorate: [0, 1].map((i) => actor(`ZXSubProbe${num + i}`, num + i, "BON1", true)).join("\n"),
        gldefs: colours.map((c, i) => light(`ZXSUB${num + i}`, `ZXSubProbe${num + i}`, "BON1A", "  subtractive 1\n")
          .replace("size 56", "size 64").replace("color 0.3 0.3 0.3", `color ${c}`)).join("\n"),
      };
    },
  };
}

// [Doruk-Aksoy] A sector case whose two lights are map things of class SectorPointLight, the second with the given args[3], and no GLDEFS.
function mapSectorCase(map, title, level, arg3) {
  const c = sectorCase(map, title, level, "", 20278);
  c.labels = ["args[3] 0", `args[3] ${arg3}`];
  const build = c.build;
  c.build = () => {
    const b = build();
    b.map.things = b.map.things.filter((t) => t.type === 1);
    [0, arg3].forEach((a, i) => b.map.thing({ x: [1600, 5000][i], y: 2584, angle: 0, type: 9803, height: 1000, arg0: 77, arg1: 77, arg2: 77, arg3: a }));
    return { map: b.map, decorate: "", gldefs: "" };
  };
  return c;
}

// [Doruk-Aksoy] A reach case of two sector lights, each standing in a 16-unit sector of the given light level inside the black room, the first unscaled.
function sectorCase(map, title, level, scale, num) {
  const c = reachCase(map, title, 0, 0, num);
  c.labels = ["no scale", `scale ${scale}`];
  const build = c.build;
  c.build = () => {
    const b = build();
    for (const x of [1600, 5000]) b.map.block(x - 8, 2576, x + 8, 2592, { heightfloor: 0, heightceiling: 2048, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: level }, 0);
    const probe = (i, extra) => `sectorlight ZXSECTOR${num + i}\n{\n  color 0.3 0.3 0.3\n${extra}}\nobject ZXSizeProbe${num + i}\n{\n  frame BON1A { light ZXSECTOR${num + i} }\n}\n`;
    b.gldefs = [probe(0, ""), probe(1, `  scale ${scale}\n`)].join("\n");
    return b;
  };
  return c;
}

// [Doruk-Aksoy] Two lights 1000 up a 2048 wall 2536 from the player, beyond a radius-900 light's reach of floor and ceiling, so only the wall is measured; the small one at x 1600, the large one at 5000.
function reachCase(map, title, small, large, num) {
  return {
    map, title, labels: [`size ${small}`, `size ${large}`], reference: 0, measure: "reach",
    zones: [[0.02, 0.4], [0.4, 0.98]], rows: [0.1, 0.5],
    build() {
      const m = blackRoom(7680, 2600, 2048);
      m.thing({ x: 3750, y: 64, angle: 90, type: 1 });
      m.thing({ x: 1600, y: 2584, angle: 0, type: num, height: 1000 });
      m.thing({ x: 5000, y: 2584, angle: 0, type: num + 1, height: 1000 });
      const more = this?.gldefsExtra || ["", ""];
      const probe = (i, size) => light(`ZXSIZE${num + i}`, `ZXSizeProbe${num + i}`, "BON1A", more[i]).replace("size 56", `size ${size}`);
      return {
        map: m,
        decorate: [actor(`ZXSizeProbe${num}`, num, "BON1", true), actor(`ZXSizeProbe${num + 1}`, num + 1, "BON1", true)].join("\n"),
        gldefs: (this?.gldefsPrefix || "") + [probe(0, small), probe(1, large)].join("\n"),
      };
    },
  };
}

function buildPk3(cases) {
  const built = cases.map((c) => ({ c, ...c.build() }));
  const file = path.join(out, "lightparity.pk3");
  fs.writeFileSync(file, pk3([
    ...built.map((b) => ({ name: `maps/${b.c.map}.wad`, data: wad(mapLumps(b.c.map, b.map)) })),
    { name: "decorate.txt", data: built.map((b) => b.decorate).join("\n") },
    { name: "gldefs.txt", data: built.map((b) => b.gldefs).join("\n") },
    { name: "mapinfo.txt", data: built.map((b) => b.mapinfo || "").join("\n") },
    { name: "textures/ZXGRAY.png", data: encodePNG(64, 64, Buffer.alloc(64 * 64 * 3, 128)) },
  ]));
  return file;
}

// [Doruk-Aksoy] The brightness of every pixel in a box (fractions of the picture), brightest first.
function brightness(img, x0, x1, y0, y1) {
  const vals = [];
  for (let y = Math.floor(y0 * img.h); y < y1 * img.h; y++)
    for (let x = Math.floor(x0 * img.w); x < x1 * img.w; x++) { const [r, g, b] = px(img, x, y); vals.push((r + g + b) / 3); }
  return vals.sort((a, b) => b - a);
}

const mean = (vals) => vals.reduce((s, v) => s + v, 0) / vals.length;

// [Doruk-Aksoy] Each zone's lit spot is the mean of its brightest 50 pixels, so it is found wherever each engine's camera puts it; the band's median is the unlit level.
function ratios(c, pngPath) {
  const img = decodePNG(pngPath);
  const [y0, y1] = c.rows;
  const [zx0, zx1] = [c.zones[0][0], c.zones[c.zones.length - 1][1]];
  const band = c.measure === "reach" ? columnTops(img, zx0, zx1, y0, y1) : c.measure === "dip" ? columnBottoms(img, zx0, zx1, y0, y1) : brightness(img, zx0, zx1, y0, y1);
  const base = band.sort((a, b) => b - a)[Math.floor(band.length / 2)];
  const added = c.zones.map(([x0, x1]) => (c.measure === "reach" ? reach(img, x0, x1, y0, y1, base)
    : c.measure === "dip" ? columnBottoms(img, x0, x1, y0, y1).reduce((sum, low) => sum + Math.max(0, base - low - 1), 0)
    : mean(brightness(img, x0, x1, y0, y1).slice(0, 50)) - base));
  return { base, added, ratio: added.map((a) => a / added[c.reference]) };
}

// [Doruk-Aksoy] Each column's brightest (or with darkest, darkest) pixel in a box, so a reach's unlit level is the wall's own (UZDoom draws an unlit wall at 4 over a 2 floor), not the band's.
function columnTops(img, x0, x1, y0, y1, darkest = false) {
  const tops = [];
  for (let x = Math.floor(x0 * img.w); x < x1 * img.w; x++) {
    let top = darkest ? 255 : 0;
    for (let y = Math.floor(y0 * img.h); y < y1 * img.h; y++) { const [r, g, b] = px(img, x, y); const v = (r + g + b) / 3; top = darkest ? Math.min(top, v) : Math.max(top, v); }
    tops.push(top);
  }
  return tops;
}
const columnBottoms = (img, x0, x1, y0, y1) => columnTops(img, x0, x1, y0, y1, true);

// [Doruk-Aksoy] A light's reach along a wall facing the camera: each column's brightest pixel over the unlit level, summed, which grows with the light's radius in any field of view.
function reach(img, x0, x1, y0, y1, base) {
  return columnTops(img, x0, x1, y0, y1).reduce((sum, top) => sum + Math.max(0, top - base - 1), 0);
}

const cases = flag("--only") ? CASES.filter((c) => c.map === flag("--only")) : CASES;
// [Doruk-Aksoy] Only the cases run go in the pk3, so a build that lacks a newer case's DECORATE flag still loads the rest.
const pk3File = buildPk3(cases);
let ok = true;
for (const c of cases) {
  const uz = ratios(c, uzdoomShot(pk3File, c.map, out, uzBackend));
  // [Doruk-Aksoy] Ours on each API, each held to UZDoom on its own.
  const ours = [];
  for (const api of apis) ours.push({ api, r: ratios(c, await oursShot(pk3File, c.map, { port, tag: "lightparity", api })) });
  console.log(`\n${c.map} ${c.title}  (added brightness over "${c.labels[c.reference]}")`);
  console.log(`${"".padEnd(20)}  ${`UZDoom ${uzBackend}`.padStart(13)}${ours.map((o) => o.api.padStart(8)).join("")}`);
  c.labels.forEach((label, i) => {
    const marks = ours.map((o) => {
      const match = Math.abs(o.r.ratio[i] - uz.ratio[i]) <= Math.max(0.1, 0.15 * Math.abs(uz.ratio[i]));
      ok &&= match;
      return { value: o.r.ratio[i], match };
    });
    console.log(`${label.padEnd(20)}  ${uz.ratio[i].toFixed(2).padStart(13)}${marks.map((m) => m.value.toFixed(2).padStart(8)).join("")}  ${marks.every((m) => m.match) ? "ok" : "MISMATCH"}`);
  });
  console.log(`unlit level: UZDoom ${uz.base.toFixed(1)}, ${ours.map((o) => `${o.api} ${o.r.base.toFixed(1)}`).join(", ")}; reference added: UZDoom ${uz.added[c.reference].toFixed(1)}, ${ours.map((o) => `${o.api} ${o.r.added[c.reference].toFixed(1)}`).join(", ")}`);
}
console.log(`\nshots in ${out}`);
process.exit(ok ? 0 : 1);
