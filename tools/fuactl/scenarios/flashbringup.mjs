// [MGOOOOOO] A gun whose Select raises it in zero-tic frames and whose Ready starts its flash: exit 0 when that flash keeps running once the gun is up, which P_BringUpWeapon wiped after one tic while it cleared ps_flash after setting the new weapon's state (uzdoom@599fa7cfd1).
//
// usage:  node scenarios/flashbringup.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

// [MGOOOOOO] The gun reaches the top inside P_BringUpWeapon's own state call, as MM8BDM v6c's SelectSwap does, and its flash adds one ZXFlashTic a tic.
export const DECORATE = `actor ZXFlashTic : Inventory
{
  Inventory.MaxAmount 1000000
}
actor ZXFlashGun : Weapon
{
  +WEAPON.AMMO_OPTIONAL
  States
  {
  Spawn:
    PIST A -1
    Stop
  Select:
    PISG AAAAAAAAAAAAAAAAAAAAAA 0 A_Raise
    PISG A 1 A_Raise
    Loop
  Deselect:
    PISG A 1 A_Lower
    Loop
  Ready:
    PISG A 0 A_GunFlash
  ReadyLoop:
    PISG A 1 A_WeaponReady
    Loop
  Fire:
    PISG A 1
    Goto ReadyLoop
  Flash:
    TNT1 A 0 A_GiveInventory("ZXFlashTic", 1)
    TNT1 A 1
    Loop
  }
}
`;

export function buildFlashBringUpWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m), { name: "DECORATE", data: DECORATE }]);
}

export const SIDES = [{ name: "vk", noGl: true }];

// [MGOOOOOO] How many tics the flash has run, from printinv's line for ZXFlashTic, 0 when it never ran.
export function flashTics(text) {
  const m = /ZXFlashTic #\d+ \((\d+)\//.exec(String(text));
  return m ? Number(m[1]) : 0;
}

// [MGOOOOOO] The flash must have started, or the gun never came up and nothing was tested, and must still be counting a second later.
export function judgeFlashBringUp(label, { early, late }) {
  if (early < 1) return { line: `FAIL ${label}: the flash never started, so the gun never came up and nothing was tested`, failed: 1 };
  if (!(late > early)) return { line: `FAIL ${label}: the flash ran ${late} tic(s) and stopped as the gun came up`, failed: 1 };
  return { line: `PASS ${label}: the flash kept running, ${early} tics and then ${late}`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  await c.rpc("sim.resume", {}).catch(() => {});
  await cap.exec(c, "give ZXFlashGun");
  await cap.exec(c, "use ZXFlashGun");
  await cap.waitTics(c, 70);
  const early = flashTics(await cap.exec(c, "printinv", { quietMs: 400 }));
  await cap.waitTics(c, 35);
  const late = flashTics(await cap.exec(c, "printinv", { quietMs: 400 }));
  return judgeFlashBringUp(label, { early, late });
}

runScenarioIfMain(import.meta.url, () => ({ name: "flashbringup", wad: buildFlashBringUpWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
