// [Doruk-Aksoy] The engine glue of features/acs-random: streams as FRandoms, found by key, made and seeded on first use, and named in savegames.

#include "features/acs-random/acsrandom.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c_console.h"
#include "doomtype.h"
#include "m_crc32.h"
#include "m_png.h"
#include "m_random.h"
#include "features/acs-random/computation/random_compute.h"

extern DWORD rngseed;

namespace zx { namespace acsrandom {

namespace {

const DWORD kChunkId = MAKE_ID('f', 'R', 'n', 'd');

// [Doruk-Aksoy] The streams scripts made this session, by key; each FRandom lives as long as the process, as DECORATE's named ones do.
std::map<std::string, FRandom *> g_streams;
std::set<std::string> g_warned;

// [Doruk-Aksoy] The stream of `key`, made and seeded from the game's rngseed when new, as every stream is at a level's start; NULL when its key's CRC is
// another stream's, which would draw on that one.
FRandom *StreamOfKey(const std::string &key)
{
	auto found = g_streams.find(key);
	if (found != g_streams.end())
		return found->second;
	const DWORD crc = CalcCRC32(reinterpret_cast<const BYTE *>(key.c_str()), unsigned(key.size()));
	if (crc == 0 || FRandom::StaticHasCRC(crc))
	{
		if (g_warned.insert(key).second)
			Printf(TEXTCOLOR_ORANGE "fua_Random: the stream name in \"%s\" clashes with another random stream, so it draws nothing; pick another name.\n", key.c_str());
		return NULL;
	}
	auto made = g_streams.emplace(key, nullptr).first;
	FRandom *rng = new FRandom(made->first.c_str());
	rng->Init(rngseed);
	made->second = rng;
	return rng;
}

FRandom *Stream(const char *name, bool clientside)
{
	const std::string key = ComputeStreamKey(name, clientside);
	return key.empty() ? NULL : StreamOfKey(key);
}

} // namespace

// [Doruk-Aksoy] Each draw takes the stream's own 32-bit number, never FRandom's 0-255 one, which compat_oldrandom turns into the global P_Random.
int Random(const char *name, bool clientside, int min, int max)
{
	FRandom *rng = Stream(name, clientside);
	return rng != NULL ? ComputeRandomRange(min, max, rng->GenRand32()) : 0;
}

int FixedRandom(const char *name, bool clientside, int min, int max)
{
	FRandom *rng = Stream(name, clientside);
	return rng != NULL ? ComputeFixedRange(min, max, rng->GenRand32()) : 0;
}

int SetSeed(const char *name, bool clientside, int seed)
{
	FRandom *rng = Stream(name, clientside);
	if (rng == NULL)
		return 0;
	rng->Init(DWORD(seed));
	return 1;
}

void WriteStreams(FILE *file)
{
	if (g_streams.empty())
		return;
	std::vector<std::string> keys;
	for (const auto &stream : g_streams)
		keys.push_back(stream.first);
	const std::vector<uint8_t> chunk = ComputeEncodeKeys(keys);
	M_AppendPNGChunk(file, kChunkId, chunk.data(), DWORD(chunk.size()));
}

void ReadStreams(PNGHandle *png)
{
	const unsigned int length = M_FindPNGChunk(png, kChunkId);
	if (length == 0)
		return;
	std::vector<uint8_t> chunk(length);
	const size_t read = fread(chunk.data(), 1, length, png->File->GetFile());
	png->File->ResetFilePtr();
	std::vector<std::string> keys;
	if (read != length || !ComputeDecodeKeys(chunk.data(), chunk.size(), keys))
	{
		Printf(TEXTCOLOR_ORANGE "fua_Random: the savegame's stream names are damaged, so its named streams start over.\n");
		return;
	}
	for (const std::string &key : keys)
		StreamOfKey(key);
}

}} // namespace zx::acsrandom
