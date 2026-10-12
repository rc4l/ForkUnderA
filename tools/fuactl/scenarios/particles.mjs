// [rc4l] A particle fountain in view on Vulkan in standalone mode: exit 0 when the backend is handed the fountain's particles, which standalone takes from its own walk since GL's walk was where they came from.
//
// usage:  node scenarios/particles.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareCounter } from "../src/template.mjs";

const RED_PARTICLE_FOUNTAIN = 9027;

export function buildParticlesWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 384, angle: 90, type: RED_PARTICLE_FOUNTAIN });
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "fountain", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] A fountain keeps dozens of particles in the air; none drawn is the fault.
export const COUNTER = { command: "fua_particles", label: "particles drawn", pattern: "particles: (\\d+) drawn", floor: 1 };

export const SIDES = [{ name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "particles", wad: buildParticlesWad(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES }), compareCounter);
