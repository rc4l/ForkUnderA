// [rc4l] Implementation of the pure MIDI-device-selection computation. No engine
// dependencies, so both the engine and the standalone test build compile this TU.
#include "sound/computation/midi_device_compute.h"

int ComputeMidiDeviceDefault(int requestedDevice, int sndMidiDevice,
	bool hasFluidsynth, bool isWin32, bool noFmod)
{
	// An explicitly-selected device is honored as-is.
	if (requestedDevice != ZX_MDEV_DEFAULT)
		return requestedDevice;

	// Without FMOD there's no FMOD soft-synth, so the historical default can't play
	// MIDI — fall back to the always-available built-in OPL2 synth.
	const int defaultSynth = noFmod ? ZX_MDEV_OPL : ZX_MDEV_FMOD;

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
