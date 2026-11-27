// [MGOOOOOO] A gun that spawns a one-tic projectile box ahead every tic, as MM8BDM classes make their hitboxes: exit 0 when the hitbox overlay draws that box with cl_fua_hitbox_onetic on and not with it off, which it never drew since the box is gone within the tic that made it.
//
// usage:  node scenarios/hitboxonetic.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch, grab } from "../src/template.mjs";
import { resolveEngine, slashed } from "../src/launch.mjs";
import { decodePNG, px } from "../src/pngcore.mjs";
import * as cap from "../src/capture.mjs";

// [MGOOOOOO] The box is spawned in the gun's Ready, so a weapon spawns it before the thinkers run and it ticks away in that same tic.
export const DECORATE = `actor ZXOneTicBox
{
  PROJECTILE
  RenderStyle None
  Radius 24
  Height 32
  States
  {
  Spawn:
    TNT1 A 1
    Stop
  }
}
actor ZXBoxGun : Weapon
{
  +WEAPON.AMMO_OPTIONAL
  States
  {
  Spawn:
    PIST A -1
    Stop
  Select:
    PISG A 1 A_Raise
    Loop
  Deselect:
    PISG A 1 A_Lower
    Loop
  Ready:
    PISG A 0 A_SpawnItemEx("ZXOneTicBox", 160, 0, 8, 0, 0, 0, 0, 0)
    PISG A 1 A_WeaponReady
    Loop
  Fire:
    PISG A 1
    Goto Ready
  }
}
`;

export function buildHitboxOneTicWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m), { name: "DECORATE", data: DECORATE }]);
}

export const SIDES = [{ name: "gl", gl: true }, { name: "vk", noGl: true }];
const AT = { x: 256, y: 64, z: 0, angle: 90, pitch: 0 };

// [MGOOOOOO] The overlay's own box hidden and its colour a magenta nothing else in the room has, so every magenta pixel is the one-tic box.
export const OVERLAY = ["cl_fua_hitbox 1", "cl_fua_hitbox_self 0", "cl_fua_hitbox_attackbox 0", 'cl_fua_hitbox_color "ff 00 ff"',
  "cl_fua_hitbox_linewidth 3", "r_drawplayersprites 0"];

// [MGOOOOOO] The pixels within 48 of magenta on every channel.
export function magentaPixels(img) {
  let n = 0;
  for (let y = 0; y < img.h; y++) for (let x = 0; x < img.w; x++) {
    const [r, g, b] = px(img, x, y);
    if (r >= 207 && g <= 48 && b >= 207) n++;
  }
  return n;
}

// [MGOOOOOO] Drawn with the option on (a box's edges at 160 units are hundreds of pixels), and not with it off.
export const MIN_DRAWN = 200, MAX_HIDDEN = 20;
export function judgeHitboxOneTic(label, { on, off }) {
  if (on < MIN_DRAWN) return { line: `FAIL ${label}: the one-tic box is not drawn (${on} magenta pixels with cl_fua_hitbox_onetic 1)`, failed: 1 };
  if (off > MAX_HIDDEN) return { line: `FAIL ${label}: the one-tic box is still drawn with cl_fua_hitbox_onetic 0 (${off} magenta pixels)`, failed: 1 };
  return { line: `PASS ${label}: the one-tic box drawn (${on} magenta pixels), and hidden with the option off (${off})`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  const exe = slashed(resolveEngine());
  const tag = `hitboxonetic_${label.replace(/\W+/g, "_")}`;
  const side = SIDES.find((s) => label.endsWith(s.name));
  await c.rpc("sim.resume", {}).catch(() => {});
  for (const line of [...OVERLAY, "give ZXBoxGun", "use ZXBoxGun"]) await cap.exec(c, line);
  await cap.waitTics(c, 35);
  await cap.exec(c, "cl_fua_hitbox_onetic 1");
  const on = await grab(c, exe, `${tag}_on`, AT, side);
  await cap.exec(c, "cl_fua_hitbox_onetic 0");
  const off = await grab(c, exe, `${tag}_off`, AT, side);
  if (!on || !off) return { line: `FAIL ${label}: a capture came back empty`, failed: 1 };
  return judgeHitboxOneTic(label, { on: magentaPixels(decodePNG(on)), off: magentaPixels(decodePNG(off)) });
}

runScenarioIfMain(import.meta.url, () => ({ name: "hitboxonetic", wad: buildHitboxOneTicWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
