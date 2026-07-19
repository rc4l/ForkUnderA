// [rc4l] Pure MIDI-device-selection logic, extracted from MIDIStreamer::SelectMIDIDevice
// so it can be unit-tested without linking the engine. Build-time choices (fluidsynth
// availability, platform, FMOD presence) are passed in as runtime args so every
// combination is coverable. Values mirror EMidiDevice (s_sound.h); the caller
// static_asserts they stay in sync. Implementation in midi_device_compute.cpp.
#ifndef ZX_MIDI_DEVICE_COMPUTE_H
#define ZX_MIDI_DEVICE_COMPUTE_H

// [rc4l] MIDI device ids, mirroring EMidiDevice in s_sound.h.
enum
{
	ZX_MDEV_DEFAULT    = -1,
	ZX_MDEV_MMAPI      = 0,
	ZX_MDEV_OPL        = 1,
	ZX_MDEV_FMOD       = 2,
	ZX_MDEV_TIMIDITY   = 3,
	ZX_MDEV_FLUIDSYNTH = 4,
	ZX_MDEV_GUS        = 5,
};

// [rc4l] Resolve the concrete MIDI device to play on from the requested device and config.
// requestedDevice: the song's explicit device, or ZX_MDEV_DEFAULT for "use the cvar".
// sndMidiDevice: the snd_mididevice cvar value. hasFluidsynth/isWin32/noFmod: build flags.
int ComputeMidiDeviceDefault(int requestedDevice, int sndMidiDevice,
	bool hasFluidsynth, bool isWin32, bool noFmod);

#endif // ZX_MIDI_DEVICE_COMPUTE_H
