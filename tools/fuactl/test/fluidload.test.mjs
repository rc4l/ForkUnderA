import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeLoad, wantedLibraries, oneNoteMidi, buildFluidLoadPk3, SONG } from "../scenarios/fluidload.mjs";

test("the failure line must name every library newest first, and a loaded library prints none", () => {
  const win = wantedLibraries("win32");
  assert.deepEqual(judgeLoad(`\x1cGCould not load ${win}\n`, "win32"), { faults: [], loaded: false });
  assert.deepEqual(judgeLoad("Could not load fluidsynth64.dll or libfluidsynth64.dll\n", "win32"),
    { faults: [`the FluidSynth device tried fluidsynth64.dll or libfluidsynth64.dll, not ${win}`], loaded: false });
  assert.deepEqual(judgeLoad(`Could not load ${wantedLibraries("linux")}: libfluidsynth.so.1: cannot open\n`, "linux"), { faults: [], loaded: false });
  assert.deepEqual(judgeLoad("", "win32"), { faults: [], loaded: true });
});

test("the song is a one-track MIDI file in the pk3's music folder", () => {
  const midi = oneNoteMidi();
  assert.equal(midi.subarray(0, 4).toString("latin1"), "MThd");
  assert.equal(midi.subarray(14, 18).toString("latin1"), "MTrk");
  assert.equal(midi.readUInt32BE(18), midi.length - 22);
  assert.ok(buildFluidLoadPk3().toString("latin1").includes(`music/${SONG}.mid`));
});
