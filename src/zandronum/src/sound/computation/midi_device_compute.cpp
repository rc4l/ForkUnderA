// [rc4l] Implementation of the pure MIDI-device-selection computation. No engine
// dependencies, so both the engine and the standalone test build compile this TU.
#include "sound/computation/midi_device_compute.h"

int ComputeMidiDeviceDefault(int requestedDevice, int sndMidiDevice,
	bool hasFluidsynth, bool isWin32)
{
	// An explicitly-selected device is honored as-is.
	if (requestedDevice != ZX_MDEV_DEFAULT)
		return requestedDevice;

	// FMOD is gone from the tree, so the historical FMOD soft-synth default cannot play
	// MIDI — the always-available built-in OPL2 synth takes its place.
	const int defaultSynth = ZX_MDEV_OPL;

	switch (sndMidiDevice)
	{
	case -1: return defaultSynth;
	case -2: return ZX_MDEV_TIMIDITY;
	case -3: return ZX_MDEV_OPL;
	case -4: return ZX_MDEV_GUS;
	case -5:
		// Fluidsynth only when compiled in; otherwise this value falls through to the
		// platform default like any other unknown value.
		if (hasFluidsynth)
			return ZX_MDEV_FLUIDSYNTH;
		return isWin32 ? ZX_MDEV_MMAPI : defaultSynth;
	default:
		return isWin32 ? ZX_MDEV_MMAPI : defaultSynth;
	}
}
