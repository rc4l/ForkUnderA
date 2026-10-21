#include "features/sound-decoders/builtindecoder.h"

#include "files.h"

#include <utility>
#include <vector>

bool BuiltinDecoder::open(FileReader *reader)
{
	const long left = reader->GetLength() - reader->Tell();
	if (left <= 0) return false;
	std::vector<unsigned char> bytes(static_cast<size_t>(left));
	if (reader->Read(bytes.data(), left) != left) return false;
	return zx::ComputePcmOpen(Stream, std::move(bytes));
}

void BuiltinDecoder::getInfo(int *samplerate, ChannelConfig *chans, SampleType *type)
{
	*samplerate = static_cast<int>(Stream.rate);
	*chans = Stream.channels == 2 ? ChannelConfig_Stereo : ChannelConfig_Mono;
	*type = SampleType_Int16;
}

size_t BuiltinDecoder::read(char *buffer, size_t bytes)
{
	return zx::ComputePcmRead(Stream, buffer, bytes);
}

bool BuiltinDecoder::seek(size_t ms_offset, bool ms, bool /*mayrestart*/)
{
	return zx::ComputePcmSeek(Stream, ms_offset, ms);
}

size_t BuiltinDecoder::getSampleOffset()
{
	return static_cast<size_t>(Stream.position);
}

size_t BuiltinDecoder::getSampleLength()
{
	return static_cast<size_t>(Stream.frames);
}
