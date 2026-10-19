#include "pcmdecode_compute.h"
#include "pcmdecode_test_fixtures.h"

#include <gtest/gtest.h>

#include <climits>
#include <cstring>
#include <string>
#include <vector>

using zx::PcmFormat;
using zx::PcmStream;

namespace
{

std::vector<unsigned char> Bytes(const unsigned char *data, size_t size)
{
	return std::vector<unsigned char>(data, data + size);
}

std::vector<unsigned char> Bytes(const std::string &text)
{
	return std::vector<unsigned char>(text.begin(), text.end());
}

void Put32(std::vector<unsigned char> &out, unsigned int v)
{
	for (int i = 0; i < 4; i++) out.push_back(static_cast<unsigned char>(v >> (8 * i)));
}

void Put16(std::vector<unsigned char> &out, unsigned int v)
{
	out.push_back(static_cast<unsigned char>(v));
	out.push_back(static_cast<unsigned char>(v >> 8));
}

// [rc4l] A PCM WAV whose sample n of every channel is n * step, as unsigned 8-bit or signed 16-bit.
std::vector<unsigned char> MakeWav(unsigned int channels, unsigned int bits, unsigned int frames, int step)
{
	const unsigned int rate = 11025, size = frames * channels * bits / 8;
	std::vector<unsigned char> out = Bytes(std::string("RIFF"));
	Put32(out, 36 + size);
	for (char c : std::string("WAVEfmt ")) out.push_back(static_cast<unsigned char>(c));
	Put32(out, 16);
	Put16(out, 1);
	Put16(out, channels);
	Put32(out, rate);
	Put32(out, rate * channels * bits / 8);
	Put16(out, channels * bits / 8);
	Put16(out, bits);
	for (char c : std::string("data")) out.push_back(static_cast<unsigned char>(c));
	Put32(out, size);
	for (unsigned int n = 0; n < frames; n++)
		for (unsigned int c = 0; c < channels; c++)
		{
			if (bits == 8) out.push_back(static_cast<unsigned char>(128 + static_cast<int>(n) * step));
			else Put16(out, static_cast<unsigned int>(static_cast<int>(n) * step) & 0xFFFF);
		}
	return out;
}

// [rc4l] Every frame left in the stream, read in pieces of `piece` bytes.
std::vector<short> ReadAll(PcmStream &stream, size_t piece = 256)
{
	std::vector<short> all;
	std::vector<char> buffer(piece);
	for (size_t got; (got = zx::ComputePcmRead(stream, buffer.data(), buffer.size())) > 0;)
	{
		const size_t at = all.size();
		all.resize(at + got / 2);
		memcpy(&all[at], buffer.data(), got);
	}
	return all;
}

}

TEST(PcmFormat, TellsTheFormatsApartByTheirFirstBytes)
{
	struct Case { std::vector<unsigned char> bytes; PcmFormat format; };
	const Case cases[] = {
		{ Bytes(std::string("RIFF\0\0\0\0WAVE", 12)), PcmFormat::Wav },
		{ Bytes(std::string("RIFF\0\0\0\0AVI ", 12)), PcmFormat::None },
		{ Bytes(std::string("RIFF\0\0\0\0WAV", 11)), PcmFormat::None },
		{ Bytes(std::string("fLaC")), PcmFormat::Flac },
		{ Bytes(std::string("OggS")), PcmFormat::Vorbis },
		{ Bytes(std::string("ID3\3")), PcmFormat::Mp3 },
		{ { 0xFF, 0xFB, 0x90, 0x64 }, PcmFormat::Mp3 },
		{ { 0x03, 0x00, 0x11, 0x2B }, PcmFormat::None },
		{ {}, PcmFormat::None },
	};
	for (const Case &c : cases) EXPECT_EQ(zx::ComputePcmFormat(c.bytes.data(), c.bytes.size()), c.format);
}

TEST(PcmFormat, AnMpegFrameNeedsItsSyncAndNoReservedField)
{
	const std::vector<std::vector<unsigned char>> notFrames = {
		{ 0xFF, 0xFB, 0x90 },          // [rc4l] Shorter than a header.
		{ 0xFE, 0xFB, 0x90, 0x64 },    // [rc4l] First sync byte.
		{ 0xFF, 0x1B, 0x90, 0x64 },    // [rc4l] Last three sync bits.
		{ 0xFF, 0xEB, 0x90, 0x64 },    // [rc4l] Reserved version.
		{ 0xFF, 0xF9, 0x90, 0x64 },    // [rc4l] Reserved layer.
		{ 0xFF, 0xFB, 0x00, 0x64 },    // [rc4l] Free-format bitrate.
		{ 0xFF, 0xFB, 0xF0, 0x64 },    // [rc4l] Reserved bitrate.
		{ 0xFF, 0xFB, 0x9C, 0x64 },    // [rc4l] Reserved sample rate.
	};
	for (const std::vector<unsigned char> &b : notFrames) EXPECT_EQ(zx::ComputePcmFormat(b.data(), b.size()), PcmFormat::None);
}

TEST(PcmDecode, ReadsAnEightBitMonoWavToSixteenBits)
{
	PcmStream stream;
	ASSERT_TRUE(zx::ComputePcmOpen(stream, MakeWav(1, 8, 100, 1)));
	EXPECT_EQ(stream.format, PcmFormat::Wav);
	EXPECT_EQ(stream.rate, 11025u);
	EXPECT_EQ(stream.channels, 1u);
	EXPECT_EQ(stream.frames, 100u);
	const std::vector<short> all = ReadAll(stream);
	ASSERT_EQ(all.size(), 100u);
	EXPECT_EQ(all[0], 0);
	EXPECT_EQ(all[10], 10 << 8);
	EXPECT_EQ(stream.position, 100u);
}

TEST(PcmDecode, ReadsAStereoWavInterleavedAndWholeFramesOnly)
{
	PcmStream stream;
	ASSERT_TRUE(zx::ComputePcmOpen(stream, MakeWav(2, 16, 50, 3)));
	char buffer[7];
	// [rc4l] Seven bytes hold one stereo frame of four, so one is read.
	EXPECT_EQ(zx::ComputePcmRead(stream, buffer, sizeof(buffer)), 4u);
	EXPECT_EQ(stream.position, 1u);
	const std::vector<short> rest = ReadAll(stream);
	ASSERT_EQ(rest.size(), 98u);
	EXPECT_EQ(rest[0], 3);
	EXPECT_EQ(rest[1], 3);
	EXPECT_EQ(rest[2], 6);
}

TEST(PcmDecode, SeeksByFrameAndByTime)
{
	PcmStream stream;
	ASSERT_TRUE(zx::ComputePcmOpen(stream, MakeWav(1, 16, 2000, 1)));
	ASSERT_TRUE(zx::ComputePcmSeek(stream, 40, false));
	EXPECT_EQ(stream.position, 40u);
	short sample = 0;
	ASSERT_EQ(zx::ComputePcmRead(stream, reinterpret_cast<char *>(&sample), 2), 2u);
	EXPECT_EQ(sample, 40);
	// [rc4l] 100 ms at 11025 Hz is frame 1102.
	ASSERT_TRUE(zx::ComputePcmSeek(stream, 100, true));
	EXPECT_EQ(stream.position, 1102u);
	ASSERT_EQ(zx::ComputePcmRead(stream, reinterpret_cast<char *>(&sample), 2), 2u);
	EXPECT_EQ(sample, 1102);
}

TEST(PcmDecode, KeepsOnlyMonoAndStereo)
{
	PcmStream stream;
	EXPECT_FALSE(zx::ComputePcmOpen(stream, MakeWav(3, 16, 10, 1)));
	EXPECT_EQ(stream.format, PcmFormat::None);
	EXPECT_EQ(stream.decoder, nullptr);
	EXPECT_TRUE(stream.bytes.empty());
}

TEST(PcmDecode, ReadsFlacMonoAndStereo)
{
	for (bool stereo : { false, true })
	{
		PcmStream stream;
		ASSERT_TRUE(zx::ComputePcmOpen(stream, stereo ? Bytes(kFlacStereo, sizeof(kFlacStereo)) : Bytes(kFlacMono, sizeof(kFlacMono))));
		EXPECT_EQ(stream.format, PcmFormat::Flac);
		EXPECT_EQ(stream.rate, 11025u);
		EXPECT_EQ(stream.channels, stereo ? 2u : 1u);
		EXPECT_EQ(stream.frames, 1103u);
		EXPECT_EQ(ReadAll(stream).size(), 1103u * stream.channels);
		ASSERT_TRUE(zx::ComputePcmSeek(stream, 500, false));
		EXPECT_EQ(ReadAll(stream).size(), 603u * stream.channels);
	}
}

TEST(PcmDecode, ReadsOggVorbisToItsLength)
{
	PcmStream stream;
	ASSERT_TRUE(zx::ComputePcmOpen(stream, Bytes(kVorbisMono, sizeof(kVorbisMono))));
	EXPECT_EQ(stream.format, PcmFormat::Vorbis);
	EXPECT_EQ(stream.rate, 11025u);
	EXPECT_EQ(stream.channels, 1u);
	ASSERT_GT(stream.frames, 1000u);
	EXPECT_EQ(ReadAll(stream).size(), stream.frames);
	ASSERT_TRUE(zx::ComputePcmSeek(stream, 100, false));
	EXPECT_EQ(ReadAll(stream).size(), stream.frames - 100);
	// [rc4l] Past the end, and past what stb_vorbis can count to.
	EXPECT_FALSE(zx::ComputePcmSeek(stream, static_cast<size_t>(stream.frames) + 5000, false));
	EXPECT_FALSE(zx::ComputePcmSeek(stream, static_cast<size_t>(UINT_MAX) + 1, false));
}

TEST(PcmDecode, ReadsMp3WithAndWithoutATag)
{
	for (bool tagged : { false, true })
	{
		PcmStream stream;
		ASSERT_TRUE(zx::ComputePcmOpen(stream, tagged ? Bytes(kMp3Tagged, sizeof(kMp3Tagged)) : Bytes(kMp3Mono, sizeof(kMp3Mono))));
		EXPECT_EQ(stream.format, PcmFormat::Mp3);
		EXPECT_EQ(stream.rate, 11025u);
		EXPECT_EQ(stream.channels, 1u);
		EXPECT_EQ(stream.frames, 0u);
		const size_t first = ReadAll(stream).size();
		EXPECT_GT(first, 1000u);
		ASSERT_TRUE(zx::ComputePcmSeek(stream, 0, true));
		EXPECT_EQ(ReadAll(stream).size(), first);
	}
}

TEST(PcmDecode, RefusesEachFormatsSignatureOverGarbage)
{
	const std::string garbage(64, '\x55');
	for (const std::string &magic : { std::string("RIFF\x40\0\0\0WAVE", 12), std::string("fLaC"), std::string("OggS"), std::string("ID3\3\0\0\0\0\0\0") })
	{
		PcmStream stream;
		EXPECT_FALSE(zx::ComputePcmOpen(stream, Bytes(magic + garbage))) << magic.substr(0, 4);
		EXPECT_EQ(stream.decoder, nullptr);
	}
	PcmStream stream;
	EXPECT_FALSE(zx::ComputePcmOpen(stream, Bytes(std::string("MThd") + garbage)));
}

TEST(PcmDecode, AClosedStreamReadsNothingAndCannotSeek)
{
	PcmStream stream;
	char buffer[16];
	EXPECT_EQ(zx::ComputePcmRead(stream, buffer, sizeof(buffer)), 0u);
	EXPECT_FALSE(zx::ComputePcmSeek(stream, 0, false));
	ASSERT_TRUE(zx::ComputePcmOpen(stream, MakeWav(1, 16, 10, 1)));
	zx::ComputePcmClose(stream);
	EXPECT_EQ(zx::ComputePcmRead(stream, buffer, sizeof(buffer)), 0u);
	EXPECT_EQ(stream.channels, 0u);
}

TEST(PcmDecode, OpeningAgainReplacesTheOldDecoder)
{
	PcmStream stream;
	ASSERT_TRUE(zx::ComputePcmOpen(stream, MakeWav(1, 16, 10, 1)));
	ASSERT_TRUE(zx::ComputePcmOpen(stream, Bytes(kFlacMono, sizeof(kFlacMono))));
	EXPECT_EQ(stream.format, PcmFormat::Flac);
	EXPECT_EQ(stream.frames, 1103u);
}
