// [rc4l] Which FluidSynth libraries a MIDI song on the FluidSynth device tries (zandronum@ceb98a637090): exit 0 when the 2.2 and 2.1 libraries are tried before the old names, read from the failure line on a machine with none of them.
//
// usage:  node scenarios/fluidload.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const SONG = "zxmidi";
// [rc4l] FluidSynth's device, set from the console since a command line takes -5 for a switch.
export const FLUIDSYNTH_DEVICE = -5;

// [rc4l] The libraries in the order a build on `platform` tries them, as the engine's failure line joins them.
export function wantedLibraries(platform) {
  if (platform === "win32") return "libfluidsynth-3.dll, libfluidsynth-2.dll, fluidsynth64.dll, or libfluidsynth64.dll";
  return "libfluidsynth.so.3, libfluidsynth.so.2, or libfluidsynth.so.1";
}

// [rc4l] A one-note format 0 MIDI file, so the song opens a MIDI device.
export function oneNoteMidi() {
  const events = Buffer.from([0x00, 0x90, 0x3c, 0x40, 0x60, 0x80, 0x3c, 0x00, 0x00, 0xff, 0x2f, 0x00]);
  const header = Buffer.from([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0, 96]);
  const track = Buffer.concat([Buffer.from("MTrk", "latin1"), Buffer.from([0, 0, 0, events.length]), events]);
  return Buffer.concat([header, track]);
}

export function buildFluidLoadPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }, { name: `music/${SONG}.mid`, data: oneNoteMidi() }]);
}

// [rc4l] What is wrong with what playing the song printed: its failure line must name every library, newest first, and a machine that has one loads it and prints none.
export function judgeLoad(text, platform) {
  const m = /Could not load ([^\n:]*)/.exec(String(text));
  if (!m) return { faults: [], loaded: true };
  const want = wantedLibraries(platform);
  return { faults: m[1].trim() === want ? [] : [`the FluidSynth device tried ${m[1].trim()}, not ${want}`], loaded: false };
}

// [rc4l] Audible only as far as opening the song needs: music at volume 0 is never opened.
export const SIDES = [{ name: "vk", launchCvars: { snd_musicvolume: 0.001 } }];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const device = cap.echoedCvar(await cap.exec(c, `snd_mididevice ${FLUIDSYNTH_DEVICE}; snd_mididevice`), "snd_mididevice");
  if (device !== String(FLUIDSYNTH_DEVICE)) return { line: `FAIL ${label}: snd_mididevice read back ${device}, not ${FLUIDSYNTH_DEVICE}`, failed: 1 };
  const { faults, loaded } = judgeLoad(await cap.exec(c, `changemus ${SONG}`, { quietMs: 500 }), process.platform);
  await cap.exec(c, "changemus none");
  const passed = loaded ? "a FluidSynth library loaded, so no failure line named what was tried" : `the FluidSynth device tried ${wantedLibraries(process.platform)}`;
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${passed}`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "fluidload", wad: buildFluidLoadPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
