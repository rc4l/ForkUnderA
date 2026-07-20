// [rc4l] Tests for ComputeMidiDeviceDefault — the extracted MIDI-device-selection logic.
#include "gtest/gtest.h"
#include "sound/computation/midi_device_compute.h"

namespace
{
// [rc4l] An explicitly requested (non-default) device is always honored verbatim.
TEST(ComputeMidiDeviceDefault, HonorsExplicitDevice)
{
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_OPL, -1, false, false), ZX_MDEV_OPL);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_TIMIDITY, -3, true, true), ZX_MDEV_TIMIDITY);
}

// [rc4l] A config still naming the removed FMOD synth is honored as an explicit device;
// the call site maps it to the OPL synth, so nothing here has to special-case it.
TEST(ComputeMidiDeviceDefault, ExplicitFmodDeviceStillParses)
{
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_FMOD, -1, false, false), ZX_MDEV_FMOD);
}

// [rc4l] snd_mididevice -1 picks the default synth, which is OPL now that FMOD is gone.
TEST(ComputeMidiDeviceDefault, DefaultSynthIsOpl)
{
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -1, false, false), ZX_MDEV_OPL);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -1, true, false), ZX_MDEV_OPL);
}

// [rc4l] The fixed negative cvar values map to their synths.
TEST(ComputeMidiDeviceDefault, FixedNegativeValues)
{
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -2, false, false), ZX_MDEV_TIMIDITY);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -3, false, false), ZX_MDEV_OPL);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -4, false, false), ZX_MDEV_GUS);
}

// [rc4l] -5 is FluidSynth only when compiled in; otherwise it falls through like any
// other value to the platform default (MMAPI on Windows, else the default synth).
TEST(ComputeMidiDeviceDefault, FluidSynthCase)
{
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -5, /*hasFluidsynth=*/true, false), ZX_MDEV_FLUIDSYNTH);
	// no fluidsynth -> platform default
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -5, /*hasFluidsynth=*/false, /*isWin32=*/true), ZX_MDEV_MMAPI);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, -5, /*hasFluidsynth=*/false, /*isWin32=*/false), ZX_MDEV_OPL);
}

// [rc4l] Any other value (>=0 or unknown negative) is the platform default: Windows MM
// API, else the default synth.
TEST(ComputeMidiDeviceDefault, PlatformDefaultForOtherValues)
{
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, 0, false, /*isWin32=*/true), ZX_MDEV_MMAPI);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, 7, false, /*isWin32=*/false), ZX_MDEV_OPL);
	EXPECT_EQ(ComputeMidiDeviceDefault(ZX_MDEV_DEFAULT, 7, true, /*isWin32=*/false), ZX_MDEV_OPL);
}
} // namespace
