#ifndef ZX_BUILTINDECODER_H
#define ZX_BUILTINDECODER_H

#include "i_soundinternal.h"
#include "features/sound-decoders/computation/pcmdecode_compute.h"

// [rc4l] WAV, FLAC, Ogg Vorbis and MP3 through the vendored decoders, for a build without libsndfile or mpg123, as the browser's is; CreateDecoder tries it after them.
struct BuiltinDecoder : public SoundDecoder
{
	void getInfo(int *samplerate, ChannelConfig *chans, SampleType *type) override;
	size_t read(char *buffer, size_t bytes) override;
	bool seek(size_t ms_offset, bool ms, bool mayrestart) override;
	size_t getSampleOffset() override;
	size_t getSampleLength() override;

protected:
	bool open(FileReader *reader) override;

private:
	zx::PcmStream Stream;
};

#endif
