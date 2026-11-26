// [Doruk-Aksoy] Named random streams for ACS, the pure part: each stream's key, the draws from its 32-bit numbers, and the savegame chunk naming every
// stream so a load can make them before the engine restores their state.

#ifndef ZX_ACSRANDOM_COMPUTE_H
#define ZX_ACSRANDOM_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zx { namespace acsrandom {

// [Doruk-Aksoy] The ACS function numbers.
enum Function
{
	kRandom = 24070,
	kFRandom = 24071,
	kSetRandomSeed = 24072,
};

// [Doruk-Aksoy] Every ACS stream's key starts with one of these, so no stream a mod names can be one of the engine's or DECORATE's; a CLIENTSIDE script's
// streams are apart from every other script's, so cosmetic draws never move a gameplay sequence, single player included, and no name crosses over.
extern const char *const kPrefix;
extern const char *const kClientPrefix;

// [Doruk-Aksoy] The longest stream name a script may give.
const size_t kMaxName = 64;

// [Doruk-Aksoy] The engine key of stream `name`, its letters folded to lower case as ZScript's names are; "" for a name that is empty or too long.
std::string ComputeStreamKey(const char *name, bool clientside);

// [Doruk-Aksoy] A whole number from `min` to `max`, both included and either way round, from one of the stream's 32-bit numbers.
int32_t ComputeRandomRange(int32_t min, int32_t max, uint32_t draw);

// [Doruk-Aksoy] A fixed-point number from `min` to `max`, both included and either way round, from one of the stream's 32-bit numbers.
int32_t ComputeFixedRange(int32_t min, int32_t max, uint32_t draw);

// [Doruk-Aksoy] The savegame chunk naming `keys`: a count, then each key's length and bytes.
std::vector<uint8_t> ComputeEncodeKeys(const std::vector<std::string> &keys);

// [Doruk-Aksoy] The keys a chunk names, false when it is cut short or names a key no stream could have.
bool ComputeDecodeKeys(const uint8_t *data, size_t size, std::vector<std::string> &keys);

// [Doruk-Aksoy] Whether `key` is one an ACS stream could have, so a load never makes a stream outside the prefixes.
bool ComputeKeyIsOurs(const std::string &key);

}} // namespace zx::acsrandom

#endif
