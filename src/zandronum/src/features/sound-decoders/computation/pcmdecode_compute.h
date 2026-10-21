#ifndef ZX_PCMDECODE_COMPUTE_H
#define ZX_PCMDECODE_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx
{

// [rc4l] The formats the built-in decoders read, told apart by their first bytes.
enum class PcmFormat
{
	None,
	Wav,
	Flac,
	Vorbis,
	Mp3,
};

struct PcmCodec;

// [rc4l] One open decoder over its own copy of the encoded bytes; ComputePcmOpen fills it, and closing or destroying it frees the decoder.
struct PcmStream
{
	PcmFormat format = PcmFormat::None;
	const PcmCodec *codec = nullptr;
	void *decoder = nullptr;
	std::vector<unsigned char> bytes;
	unsigned int rate = 0;
	unsigned int channels = 0;
	// [rc4l] 0 where the format cannot say without decoding all of it, as MP3 cannot.
	uint64_t frames = 0;
	uint64_t position = 0;

	PcmStream() = default;
	PcmStream(const PcmStream &) = delete;
	PcmStream &operator=(const PcmStream &) = delete;
	~PcmStream();
};

PcmFormat ComputePcmFormat(const unsigned char *data, size_t size);

// [rc4l] Opens WAV, FLAC, Ogg Vorbis or MP3 bytes, keeping only mono or stereo, as the engine plays nothing else.
bool ComputePcmOpen(PcmStream &stream, std::vector<unsigned char> bytes);

// [rc4l] Decodes up to `bytes` of 16-bit interleaved samples into `buffer`, whole frames only; returns the bytes written, 0 at the end.
size_t ComputePcmRead(PcmStream &stream, char *buffer, size_t bytes);

// [rc4l] Moves to a frame, or to a time when `ms` is set, as SoundDecoder::seek asks.
bool ComputePcmSeek(PcmStream &stream, size_t offset, bool ms);

void ComputePcmClose(PcmStream &stream);

}

#endif
