// [rc4l] Pure OpenAL sample-format / buffer-sizing math, extracted from oalsound.cpp so it
// can be unit-tested without linking OpenAL or the engine. Maps decoder channel/sample
// descriptions and stream flags to AL buffer formats and frame sizes, and rounds buffer
// sizes to whole frames. The AL_FORMAT_* / ChannelConfig / SampleType / stream-flag values
// are mirrored here and static_asserted in the caller. Implementation in
// oal_format_compute.cpp.
#ifndef ZX_OAL_FORMAT_COMPUTE_H
#define ZX_OAL_FORMAT_COMPUTE_H

// [rc4l] AL buffer formats, mirroring the al.h / oalsound.h AL_FORMAT_* macros.
enum
{
	ZX_AL_NONE                   = 0,
	ZX_AL_FORMAT_MONO8           = 0x1100,
	ZX_AL_FORMAT_MONO16          = 0x1101,
	ZX_AL_FORMAT_STEREO8         = 0x1102,
	ZX_AL_FORMAT_STEREO16        = 0x1103,
	ZX_AL_FORMAT_MONO_FLOAT32    = 0x10010,
	ZX_AL_FORMAT_STEREO_FLOAT32  = 0x10011,
};

// [rc4l] Channel layouts, mirroring enum ChannelConfig in sound/i_soundinternal.h.
enum
{
	ZX_CHANNELCONFIG_MONO   = 0,
	ZX_CHANNELCONFIG_STEREO = 1,
};

// [rc4l] Sample types, mirroring enum SampleType in sound/i_soundinternal.h.
enum
{
	ZX_SAMPLETYPE_UINT8 = 0,
	ZX_SAMPLETYPE_INT16 = 1,
};

// [rc4l] Stream description flags, mirroring SoundStream's enum in sound/i_sound.h.
enum
{
	ZX_STREAM_MONO  = 1,
	ZX_STREAM_BITS8 = 2,
	ZX_STREAM_BITS32 = 4,
	ZX_STREAM_FLOAT = 8,
};

// [rc4l] The AL format plus the frame (one sample across all channels) size in bytes.
struct ZxSampleFormat
{
	int format;     // [rc4l] AL format, or ZX_AL_NONE if the combination is unsupported
	int sampleSize; // [rc4l] bytes per frame (meaningful only when format != ZX_AL_NONE)
};

// [rc4l] Map a decoder's channel layout + sample type to the AL buffer format and frame size.
ZxSampleFormat ComputeSampleFormat(int channelConfig, int sampleType);

// [rc4l] Number of channels for a channel layout (0 for an unknown layout).
int ComputeChannelCount(int channelConfig);

// [rc4l] Resolve the AL buffer format for a stream from its flags; float needs the
// AL_EXT_FLOAT32 extension (hasFloat32Ext), and 32-bit-int streams are unsupported.
int ComputeStreamFormat(int flags, bool hasFloat32Ext);

// [rc4l] Frame size in bytes for a stream described by its flags.
int ComputeStreamFrameSize(int flags);

// [rc4l] Round a buffer byte count up to a whole number of frames.
int ComputeAlignedBufferBytes(int buffbytes, int frameSize);

#endif // ZX_OAL_FORMAT_COMPUTE_H
