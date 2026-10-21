// [rc4l] features/decals' one rule, room by room, Vulkan alone: a plain floor and wall take marks; a liquid, an animated flat and wall, a noautodecals side, a scrolling floor, a glass 3D floor's top and side, a portal ceiling, and a sky and a horizon line a rocket explodes on refuse them; a 3D floor bridge's mark rides the bridge, a Transfer_Heights room's rests on its real floor and a slope's faces along it; marks survive a save and load; cl_maxdecals 0 makes none; a liquid still splashes; and all of it holds with tics on the game thread, where marks reach the frame only through the copy, and with them inline.
//
// usage:  node scenarios/decalrules.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] A liquid that splashes a marker which never goes away, so a script can count the splashes after the fact.
const TERRAIN = `splash Marker
{
	smallclass MarkerSplash
	smallclip 0
	baseclass MarkerSplash
	chunkclass None
	sound world/watersplash
}
terrain Marked
{
	splash Marker
	liquid
}
floor FWATER1 Marked
`;
const DECORATE = `actor MarkerSplash
{
  +NOINTERACTION
  States
  {
  Spawn:
    TNT1 A -1
    Stop
  }
}
actor SkyRocket : Rocket replaces Rocket
{
  Decal Scorch
  +SKYEXPLODE
}
`;
// [rc4l] An ANIMDEFS flat, which refuses decals unless it says allowdecals.
const ANIMDEFS = `flat FLOOR0_1 range FLOOR0_3 tics 8
`;

const Y0 = 2048, SIZE = 256, GAP = 512;
const BRIDGE_TAG = 11, GLASS_TAG = 12, SCROLL_TAG = 13, FAKE_TAG = 14;
export const ROOMS = ["plain", "liquid", "animated", "sky", "nodecals", "scroll", "bridge", "glass", "horizon", "fake", "slope"];
const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 };
const roomX = (name) => ROOMS.indexOf(name) * GAP;

function plainRoom(m, name, sector, walls = () => ({ side: { texturemiddle: "STARTAN2" } })) {
  const x = roomX(name);
  return m.room(x, Y0, x + SIZE, Y0 + SIZE, { ...ROOM, ...sector }, walls);
}

// [rc4l] The rooms; returns the sector numbers the judge needs.
export function buildDecalRulesWad() {
  const m = new UdmfMap();
  // [rc4l] The stacked rooms come first, at the origin, as the helper places them: the lower one's ceiling is a portal.
  m.stackedRooms({ dx: 4096 });
  plainRoom(m, "plain", {});
  plainRoom(m, "liquid", { texturefloor: "FWATER1" });
  plainRoom(m, "animated", { texturefloor: "FLOOR0_1" }, () => ({ side: { texturemiddle: "BFALL1" } }));
  plainRoom(m, "sky", { textureceiling: "F_SKY1" });
  plainRoom(m, "nodecals", {}, (edge) => ({ side: { texturemiddle: "STARTAN2", ...(edge === "north" ? { nodecals: true } : {}) } }));
  plainRoom(m, "scroll", { id: SCROLL_TAG }, (edge) => (edge === "south"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Scroll_Floor, arg0: SCROLL_TAG, arg1: 0, arg2: 0, arg3: 64, arg4: 0 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  plainRoom(m, "bridge", { id: BRIDGE_TAG });
  // [rc4l] The glass stands in a block of its room, so it has sides to shoot as well as a top.
  const glassRoom = plainRoom(m, "glass", {});
  const gx = roomX("glass");
  m.block(gx + 96, Y0 + 160, gx + 160, Y0 + 224, { ...ROOM, id: GLASS_TAG }, glassRoom);
  plainRoom(m, "horizon", {}, (edge) => (edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } } : { side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] A Transfer_Heights room, its fake floor above the real one a shot lands on.
  const fakeRoom = plainRoom(m, "fake", { id: FAKE_TAG });
  m.room(6400, 6000, 6464, 6064, { ...ROOM, heightfloor: 32, heightceiling: 200 }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Transfer_Heights, arg0: FAKE_TAG } } : { side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] A block whose floor Plane_Align slopes from the room's 0 at its south edge up to its own 64 at its north.
  const slopeRoom = plainRoom(m, "slope", {});
  const sx = roomX("slope");
  const slopeBlock = m.block(sx + 64, Y0 + 64, sx + 192, Y0 + 192, { ...ROOM, heightfloor: 64 }, slopeRoom, () => ({ texturebottom: "STARTAN2" }),
    () => ({}), (edge) => (edge === "south" ? { special: SPECIAL.Plane_Align, arg0: 1 } : {}));
  // [rc4l] Each 3D floor's control sector, 32 to 64 high: a solid bridge, and solid glass at half alpha.
  const control = (x, tag, alpha) => m.room(x, 6000, x + 64, 6064, { heightfloor: 32, heightceiling: 64, texturefloor: "FLAT14",
    textureceiling: "FLAT14", lightlevel: 255 }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: tag, arg1: 1, arg2: 0, arg3: alpha } }
    : { side: { texturemiddle: "STARTAN2" } }));
  const bridgeControl = control(6000, BRIDGE_TAG, 255);
  control(6200, GLASS_TAG, 128);
  m.thing({ x: roomX("plain") + 128, y: Y0 + 128, angle: 90, type: 1 });
  // [rc4l] Script 2 logs how many splash markers exist.
  const behavior = acs0([PCD.BEGINPRINT, PCD.PUSHNUMBER, 0, PCD.PRINTSTRING, PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 0, PCD.THINGCOUNTNAME,
    PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE], ["splashes ", "MarkerSplash"], { open: false, number: 2 });
  return { wad: wad([{ name: "TERRAIN", data: TERRAIN }, { name: "DECORATE", data: DECORATE }, { name: "ANIMDEFS", data: ANIMDEFS },
    ...mapLumps("MAP01", m, { behavior })]), bridgeControl, fakeRoom, slopeBlock };
}

// [rc4l] Standing in a room's middle, or at dy north of it dz up, looking down at its floor, up at its ceiling, or north.
function aim(name, how, { dy = 0, z = name === "bridge" ? 64 : 0 } = {}) {
  const pitch = how === "floor" ? 80 : how === "ceiling" ? -80 : 0;
  return { x: roomX(name) + 128, y: Y0 + 128 + dy, z, angle: 90, pitch };
}

// [rc4l] fua_projdecals_stats's marks: the sector and plane each rides, the height it rests at and the way it faces.
export function parseMarks(text) {
  const re = /mark \d+: \S+ alpha [\d.]+, sector (\d+) (floor|ceiling), at z (-?[\d.]+) facing \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)/g;
  return [...String(text).matchAll(re)].map((m) => ({ sector: Number(m[1]), plane: m[2], z: Number(m[3]), normal: [Number(m[4]), Number(m[5]), Number(m[6])] }));
}

// [rc4l] fua_decal_report's answer: each surface's count by verdict, and the live marks.
export function parseDecalReport(text) {
  const counts = {};
  for (const m of String(text).matchAll(/^\s+(wall|floor|ceiling) ([A-Za-z0-9 ]+): (\d+)$/gm)) counts[`${m[1]} ${m[2]}`] = Number(m[3]);
  const live = /live: (\d+) wall decals, (\d+) floor and ceiling marks/.exec(String(text));
  return { counts, walls: live ? Number(live[1]) : null, flats: live ? Number(live[2]) : null };
}

// [rc4l] What each room must have recorded.
export const EXPECT = [
  ["floor made", "a plain floor takes a shot's mark"],
  ["wall made", "a plain wall takes one"],
  ["floor liquid", "a terrain liquid refuses it"],
  ["floor texture refuses decals", "an ANIMDEFS flat refuses it"],
  ["wall texture refuses decals", "an animated wall refuses it"],
  ["wall noautodecals side", "a nodecals side refuses it"],
  ["floor scrolling plane", "a scrolling floor refuses it"],
  ["floor translucent 3D floor", "a glass 3D floor refuses it"],
  ["wall translucent 3D floor", "a glass 3D floor's side refuses it"],
  ["ceiling portal plane", "a stacked sector's portal ceiling refuses it"],
  ["ceiling sky", "a sky a SKYEXPLODE rocket explodes on refuses its scorch"],
  ["wall horizon line", "a horizon line refuses that rocket's scorch"],
];

// [rc4l] Where the marks the planes take must rest: the bridge's on the bridge's own sector, the Transfer_Heights room's on its real floor at 0 rather than the fake one, and the slope's facing along the slope.
export const PLACED = [
  ["the bridge's mark does not ride the bridge's sector", (m, s) => m.sector === s.bridgeControl],
  ["the Transfer_Heights room's mark is not on its real floor", (m, s) => m.sector === s.fakeRoom && m.plane === "floor" && Math.abs(m.z) < 0.5],
  ["the slope's mark does not face along the slope", (m, s) => m.sector === s.slopeBlock && m.plane === "floor" && m.normal[2] > 0.8 && m.normal[2] < 0.97],
];

// [rc4l] The most tics a frame ran on the game thread over the recent frames, from fua_stats; null when it printed none.
export function parseThreadedMax(text) {
  const m = /handoff\/threaded_tics\s+tics\s+[\d.]+\s+[\d.]+\s+[\d.]+\s+([\d.]+)/.exec(String(text));
  return m ? Number(m[1]) : null;
}

// [rc4l] The verdict: every expected count above zero, the splash spawned, every placed mark where it must rest, the marks back after a load, none with cl_maxdecals 0, and the tics on the schedule the side asked for.
export function judgeDecalRules(label, { report, splashes, marks = [], sectors = {}, before, after, off, threaded }) {
  const faults = [];
  if (threaded && threaded.on && !(threaded.max > 0)) faults.push("no tic ran on the game thread, so the copy's handoff of the marks went untested");
  if (threaded && !threaded.on && threaded.max !== 0) faults.push("tics ran on the game thread with fua_gamethread 0");
  if (!report || report.walls === null) faults.push("no fua_decal_report answer");
  else for (const [key, why] of EXPECT) if (!(report.counts[key] > 0)) faults.push(`${why} (no "${key}")`);
  if (!(splashes > 0)) faults.push("the liquid did not splash");
  for (const [why, rests] of PLACED) if (!marks.some((m) => rests(m, sectors))) faults.push(why);
  if (!before || !after || before.flats === null || !(before.flats > 0) || after.flats !== before.flats || after.walls !== before.walls) {
    faults.push(`a load did not bring the marks back (${before ? `${before.walls}/${before.flats}` : "?"} before, ${after ? `${after.walls}/${after.flats}` : "?"} after)`);
  }
  if (!off || !(off.counts["floor decals off"] > 0) || !(off.counts["wall decals off"] > 0) || off.flats !== 0 || off.walls !== 0) {
    faults.push("cl_maxdecals 0 still made marks");
  }
  const seen = report ? Object.entries(report.counts).map(([k, v]) => `${k} ${v}`).join(", ") : "";
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")} [report: ${seen}]`, failed: 1 }
    : { line: `PASS ${label}: every rule refused or made as it should, ${before.flats} floor marks kept through a load`, failed: 0 };
}

// [rc4l] One shot: stood there long enough for the weapon to be ready, fire held a few tics so a raised weapon catches it, then time for it to come back.
async function shoot(c, at, { settle = 35 } = {}) {
  await c.rpc("player.setpos", at);
  await cap.waitTics(c, 12);
  await cap.exec(c, "+attack", { quietMs: 60 });
  await cap.waitTics(c, 4);
  await cap.exec(c, "-attack", { quietMs: 60 });
  await cap.waitTics(c, settle);
}

const report = async (c) => parseDecalReport(await cap.exec(c, "fua_decal_report", { quietMs: 300 }));

export async function judgeLaunch(c, inst, label, sectors = {}) {
  await cap.exec(c, "fua_render_in_background 1");
  await cap.exec(c, "fua_decal_report reset");
  await shoot(c, aim("plain", "floor"));
  await shoot(c, aim("plain", "wall"));
  await shoot(c, aim("liquid", "floor"));
  const splashes = Number((/splashes (\d+)/.exec(await cap.exec(c, "puke 2", { quietMs: 300 })) || [])[1]);
  await shoot(c, aim("animated", "floor"));
  await shoot(c, aim("animated", "wall"));
  await shoot(c, aim("nodecals", "wall"));
  await shoot(c, aim("scroll", "floor"));
  await shoot(c, aim("bridge", "floor"));
  // [rc4l] On the glass block's top, then at its south side from the room.
  await shoot(c, aim("glass", "floor", { dy: 64, z: 64 }));
  await shoot(c, aim("glass", "wall", { dy: -64 }));
  await shoot(c, aim("fake", "floor"));
  await shoot(c, aim("slope", "floor", { z: 32 }));
  const marks = parseMarks(await cap.exec(c, "fua_projdecals_stats", { quietMs: 300 }));
  await shoot(c, { x: 256, y: 256, z: 0, angle: 90, pitch: -80 });   // [rc4l] the stacked lower room's portal ceiling
  await cap.exec(c, "give RocketLauncher");
  await cap.exec(c, "use RocketLauncher");
  await cap.waitTics(c, 70);
  await shoot(c, aim("sky", "ceiling"), { settle: 70 });
  await shoot(c, aim("horizon", "wall"), { settle: 70 });
  const r = await report(c);
  const threaded = { on: /"fua_gamethread" is "true"/.test(await cap.exec(c, "fua_gamethread", { quietMs: 200 })),
    max: parseThreadedMax(await cap.exec(c, "fua_stats handoff/threaded_tics", { quietMs: 400 })) };
  await cap.exec(c, "save fuactl_decalrules.zds", { quietMs: 400 });
  await cap.waitTics(c, 10);
  await cap.exec(c, "load fuactl_decalrules.zds", { quietMs: 400 });
  await cap.waitInLevel(c);
  await cap.waitTics(c, 10);
  const after = await report(c);
  await cap.exec(c, "use Pistol");
  await cap.exec(c, "cl_maxdecals 0");
  await cap.exec(c, "fua_decal_report reset");
  await cap.waitTics(c, 70);
  await shoot(c, aim("plain", "floor"));
  await shoot(c, aim("plain", "wall"));
  const off = await report(c);
  await cap.exec(c, "cl_maxdecals 1024");
  return judgeDecalRules(label, { report: r, splashes, marks, sectors, before: r, after, off, threaded });
}

runScenarioIfMain(import.meta.url, () => {
  const built = buildDecalRulesWad();
  return { name: "decalrules", wad: built.wad, maps: ["MAP01"], sides: [{ name: "vk" }, { name: "vk-inline", cvars: { fua_gamethread: 0 } }],
    judgeLaunch: (c, inst, label) => judgeLaunch(c, inst, label, built) };
}, compareEachLaunch);
