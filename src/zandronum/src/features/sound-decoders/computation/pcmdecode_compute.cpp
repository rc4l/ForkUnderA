#include "pcmdecode_compute.h"

#include "../vendor/decoders.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <utility>

namespace zx
{

// [rc4l] One format's calls over the decoder its open made; open also fills the stream's rate, channels and frames.
struct PcmCodec
{
	void *(*open)(PcmStream &stream);
	uint64_t (*read)(void *decoder, unsigned int channels, uint64_t frames, short *out);
	bool (*seek)(void *decoder, uint64_t frame);
	void (*close)(void *decoder);
};

namespace
{

void *OpenWav(PcmStream &stream)
{
	auto *wav = new drwav;
	if (!drwav_init_memory(wav, stream.bytes.data(), stream.bytes.size(), nullptr))
	{
		delete wav;
		return nullptr;
	}
	stream.rate = wav->sampleRate;
	stream.channels = wav->channels;
	stream.frames = wav->totalPCMFrameCount;
	return wav;
}

uint64_t ReadWav(void *decoder, unsigned int, uint64_t frames, short *out)
{
	return drwav_read_pcm_frames_s16(static_cast<drwav *>(decoder), frames, out);
}

bool SeekWav(void *decoder, uint64_t frame)
{
	return drwav_seek_to_pcm_frame(static_cast<drwav *>(decoder), frame) == DRWAV_TRUE;
}

void CloseWav(void *decoder)
{
	drwav_uninit(static_cast<drwav *>(decoder));
	delete static_cast<drwav *>(decoder);
}

void *OpenFlac(PcmStream &stream)
{
	drflac *flac = drflac_open_memory(stream.bytes.data(), stream.bytes.size(), nullptr);
	if (flac == nullptr) return nullptr;
	stream.rate = flac->sampleRate;
	stream.channels = flac->channels;
	stream.frames = flac->totalPCMFrameCount;
	return flac;
}

uint64_t ReadFlac(void *decoder, unsigned int, uint64_t frames, short *out)
{
	return drflac_read_pcm_frames_s16(static_cast<drflac *>(decoder), frames, out);
}

bool SeekFlac(void *decoder, uint64_t frame)
{
	return drflac_seek_to_pcm_frame(static_cast<drflac *>(decoder), frame) == DRFLAC_TRUE;
}

void CloseFlac(void *decoder)
{
	drflac_close(static_cast<drflac *>(decoder));
}

void *OpenVorbis(PcmStream &stream)
{
	int error = 0;
	stb_vorbis *vorbis = stb_vorbis_open_memory(stream.bytes.data(), static_cast<int>(stream.bytes.size()), &error, nullptr);
	if (vorbis == nullptr) return nullptr;
	const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
	stream.rate = info.sample_rate;
	stream.channels = static_cast<unsigned int>(info.channels);
	stream.frames = stb_vorbis_stream_length_in_samples(vorbis);
	return vorbis;
}

uint64_t ReadVorbis(void *decoder, unsigned int channels, uint64_t frames, short *out)
{
	// [rc4l] stb_vorbis counts in ints, so one read asks for no more shorts than an int holds.
	const uint64_t shorts = std::min<uint64_t>(frames * channels, INT_MAX);
	return static_cast<uint64_t>(stb_vorbis_get_samples_short_interleaved(static_cast<stb_vorbis *>(decoder), static_cast<int>(channels), out, static_cast<int>(shorts)));
}

bool SeekVorbis(void *decoder, uint64_t frame)
{
	return frame <= UINT_MAX && stb_vorbis_seek(static_cast<stb_vorbis *>(decoder), static_cast<unsigned int>(frame)) != 0;
}

void CloseVorbis(void *decoder)
{
	stb_vorbis_close(static_cast<stb_vorbis *>(decoder));
}

void *OpenMp3(PcmStream &stream)
{
	auto *mp3 = new drmp3;
	if (!drmp3_init_memory(mp3, stream.bytes.data(), stream.bytes.size(), nullptr))
	{
		delete mp3;
		return nullptr;
	}
	stream.rate = mp3->sampleRate;
	stream.channels = mp3->channels;
	return mp3;
}

uint64_t ReadMp3(void *decoder, unsigned int, uint64_t frames, short *out)
{
	return drmp3_read_pcm_frames_s16(static_cast<drmp3 *>(decoder), frames, out);
}

bool SeekMp3(void *decoder, uint64_t frame)
{
	return drmp3_seek_to_pcm_frame(static_cast<drmp3 *>(decoder), frame) == DRMP3_TRUE;
}

void CloseMp3(void *decoder)
{
	drmp3_uninit(static_cast<drmp3 *>(decoder));
	delete static_cast<drmp3 *>(decoder);
}

const PcmCodec kWav = { OpenWav, ReadWav, SeekWav, CloseWav };
const PcmCodec kFlac = { OpenFlac, ReadFlac, SeekFlac, CloseFlac };
const PcmCodec kVorbis = { OpenVorbis, ReadVorbis, SeekVorbis, CloseVorbis };
const PcmCodec kMp3 = { OpenMp3, ReadMp3, SeekMp3, CloseMp3 };

const PcmCodec *CodecFor(PcmFormat format)
{
	switch (format)
	{
	case PcmFormat::Wav: return &kWav;
	case PcmFormat::Flac: return &kFlac;
	case PcmFormat::Vorbis: return &kVorbis;
	case PcmFormat::Mp3: return &kMp3;
	case PcmFormat::None: break;
	}
	return nullptr;
}

bool StartsWith(const unsigned char *data, size_t size, const char *magic, size_t at = 0)
{
	const size_t length = strlen(magic);
	return size >= at + length && memcmp(data + at, magic, length) == 0;
}

// [rc4l] An MPEG audio frame header: the sync bits, then a version, layer, bitrate and sample rate that are none of the reserved values.
bool IsMpegFrame(const unsigned char *data, size_t size)
{
	if (size < 4 || data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) return false;
	const int version = (data[1] >> 3) & 3;
	const int layer = (data[1] >> 1) & 3;
	const int bitrate = data[2] >> 4;
	const int rate = (data[2] >> 2) & 3;
	return version != 1 && layer != 0 && bitrate != 0 && bitrate != 15 && rate != 3;
}

}

PcmStream::~PcmStream()
{
	ComputePcmClose(*this);
}

PcmFormat ComputePcmFormat(const unsigned char *data, size_t size)
{
	if (StartsWith(data, size, "RIFF") && StartsWith(data, size, "WAVE", 8)) return PcmFormat::Wav;
	if (StartsWith(data, size, "fLaC")) return PcmFormat::Flac;
	if (StartsWith(data, size, "OggS")) return PcmFormat::Vorbis;
	if (StartsWith(data, size, "ID3") || IsMpegFrame(data, size)) return PcmFormat::Mp3;
	return PcmFormat::None;
}

bool ComputePcmOpen(PcmStream &stream, std::vector<unsigned char> bytes)
{
	ComputePcmClose(stream);
	const PcmFormat format = ComputePcmFormat(bytes.data(), bytes.size());
	const PcmCodec *codec = CodecFor(format);
	if (codec == nullptr) return false;
	// [rc4l] The decoders read the stream's own copy, which stays put since a stream is never copied or moved.
	stream.bytes = std::move(bytes);
	stream.format = format;
	stream.codec = codec;
	stream.decoder = codec->open(stream);
	if (stream.decoder != nullptr && (stream.channels == 1 || stream.channels == 2)) return true;
	ComputePcmClose(stream);
	return false;
}

size_t ComputePcmRead(PcmStream &stream, char *buffer, size_t bytes)
{
	if (stream.decoder == nullptr) return 0;
	const size_t frameBytes = 2 * static_cast<size_t>(stream.channels);
	const uint64_t got = stream.codec->read(stream.decoder, stream.channels, bytes / frameBytes, reinterpret_cast<short *>(buffer));
	stream.position += got;
	return static_cast<size_t>(got) * frameBytes;
}

bool ComputePcmSeek(PcmStream &stream, size_t offset, bool ms)
{
	if (stream.decoder == nullptr) return false;
	const uint64_t frame = ms ? static_cast<uint64_t>(offset) * stream.rate / 1000 : static_cast<uint64_t>(offset);
	if (!stream.codec->seek(stream.decoder, frame)) return false;
	stream.position = frame;
	return true;
}

void ComputePcmClose(PcmStream &stream)
{
	if (stream.decoder != nullptr) stream.codec->close(stream.decoder);
	stream.format = PcmFormat::None;
	stream.codec = nullptr;
	stream.decoder = nullptr;
	stream.bytes = std::vector<unsigned char>();
	stream.rate = 0;
	stream.channels = 0;
	stream.frames = 0;
	stream.position = 0;
}

}
