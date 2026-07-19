// [rc4l] Implementation of the pure OpenAL format/sizing math. No engine or OpenAL
// dependencies, so both the engine and the standalone test build compile this TU.
#include "features/openal-sound/computation/oal_format_compute.h"

ZxSampleFormat ComputeSampleFormat(int channelConfig, int sampleType)
{
	if (channelConfig == ZX_CHANNELCONFIG_MONO)
	{
		if (sampleType == ZX_SAMPLETYPE_UINT8)
			return { ZX_AL_FORMAT_MONO8, 1 };
		if (sampleType == ZX_SAMPLETYPE_INT16)
			return { ZX_AL_FORMAT_MONO16, 2 };
	}
	else if (channelConfig == ZX_CHANNELCONFIG_STEREO)
	{
		if (sampleType == ZX_SAMPLETYPE_UINT8)
			return { ZX_AL_FORMAT_STEREO8, 2 };
		if (sampleType == ZX_SAMPLETYPE_INT16)
			return { ZX_AL_FORMAT_STEREO16, 4 };
	}
	return { ZX_AL_NONE, 1 };
}

int ComputeChannelCount(int channelConfig)
{
	switch (channelConfig)
	{
	case ZX_CHANNELCONFIG_MONO:   return 1;
	case ZX_CHANNELCONFIG_STEREO: return 2;
	}
	return 0;
}

int ComputeStreamFormat(int flags, bool hasFloat32Ext)
{
	if (flags & ZX_STREAM_BITS8) // Signed or unsigned? We assume unsigned 8-bit...
		return (flags & ZX_STREAM_MONO) ? ZX_AL_FORMAT_MONO8 : ZX_AL_FORMAT_STEREO8;

	if (flags & ZX_STREAM_FLOAT)
	{
		if (hasFloat32Ext)
			return (flags & ZX_STREAM_MONO) ? ZX_AL_FORMAT_MONO_FLOAT32 : ZX_AL_FORMAT_STEREO_FLOAT32;
		return ZX_AL_NONE;
	}

	if (flags & ZX_STREAM_BITS32) // 32-bit integer PCM is unsupported.
		return ZX_AL_NONE;

	return (flags & ZX_STREAM_MONO) ? ZX_AL_FORMAT_MONO16 : ZX_AL_FORMAT_STEREO16;
}

int ComputeStreamFrameSize(int flags)
{
	int frameSize = 1;
	if (flags & ZX_STREAM_BITS8)
		frameSize *= 1;
	else if (flags & (ZX_STREAM_BITS32 | ZX_STREAM_FLOAT))
		frameSize *= 4;
	else
		frameSize *= 2;

	if (flags & ZX_STREAM_MONO)
		frameSize *= 1;
	else
		frameSize *= 2;

	return frameSize;
}

int ComputeAlignedBufferBytes(int buffbytes, int frameSize)
{
	buffbytes += frameSize - 1;
	buffbytes -= buffbytes % frameSize;
	return buffbytes;
}
