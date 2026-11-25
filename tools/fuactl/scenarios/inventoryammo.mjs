// [MGOOOOOO] A CustomInventory item that fires A_FireCustomMissile with useammo: exit 0 when the shot spends the ready weapon's AmmoUse, as on Zandronum, which uzdoom@bc206f21a's weapon-only gate had stopped.
//
// usage:  node scenarios/inventoryammo.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

// [MGOOOOOO] The gun spends 2 clips a shot and the item fires its shot, so a pistol still in hand would spend 1 and show up.
export const DECORATE = `actor ZXAmmoBall
{
  Projectile
  Speed 10
  Radius 4
  Height 4
  States
  {
  Spawn:
    TNT1 A 1
    Loop
  }
}
actor ZXAmmoShot : CustomInventory
{
  States
  {
  Pickup:
    TNT1 A 0 A_FireCustomMissile("ZXAmmoBall", 0, 1, 0, 0)
    Stop
  }
}
actor ZXAmmoGun : Weapon
{
  Weapon.AmmoType "Clip"
  Weapon.AmmoUse 2
  Weapon.AmmoGive 0
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
    PISG A 1 A_WeaponReady
    Loop
  Fire:
    PISG A 1
    Goto Ready
  }
}
`;

export function buildInventoryAmmoWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m), { name: "DECORATE", data: DECORATE }]);
}

export const SIDES = [{ name: "vk", noGl: true }];

// [MGOOOOOO] The player's clips from printinv's line for Clip, -1 when there is none.
export function clips(text) {
  const m = /\bClip #\d+ \((\d+)\//.exec(String(text));
  return m ? Number(m[1]) : -1;
}

// [MGOOOOOO] The item's shot must spend the gun's 2 clips: none is the bug, and any other count means the gun was not in hand.
export function judgeInventoryAmmo(label, { before, after }) {
  if (before < 2) return { line: `FAIL ${label}: ${before} clip(s) before the shot, so nothing was tested`, failed: 1 };
  const spent = before - after;
  if (spent === 0) return { line: `FAIL ${label}: the item's shot spent no ammo (${before} clips before and after)`, failed: 1 };
  if (spent !== 2) return { line: `FAIL ${label}: the item's shot spent ${spent} clip(s), not the gun's 2, so the gun was not in hand`, failed: 1 };
  return { line: `PASS ${label}: the item's shot spent the gun's 2 clips, ${before} then ${after}`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  await c.rpc("sim.resume", {}).catch(() => {});
  await cap.exec(c, "give ZXAmmoGun");
  await cap.exec(c, "use ZXAmmoGun");
  await cap.waitTics(c, 70);
  const before = clips(await cap.exec(c, "printinv", { quietMs: 400 }));
  await cap.exec(c, "give ZXAmmoShot");
  await cap.waitTics(c, 5);
  const after = clips(await cap.exec(c, "printinv", { quietMs: 400 }));
  return judgeInventoryAmmo(label, { before, after });
}

runScenarioIfMain(import.meta.url, () => ({ name: "inventoryammo", wad: buildInventoryAmmoWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }),
  compareEachLaunch);
