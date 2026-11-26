#include "features/acs-random/computation/random_compute.h"

#include <cstring>
#include <utility>

namespace zx { namespace acsrandom {

const char *const kPrefix = "fua.acs.play:";
const char *const kClientPrefix = "fua.acs.client:";

std::string ComputeStreamKey(const char *name, bool clientside)
{
	const size_t length = name != nullptr ? strlen(name) : 0;
	if (length == 0 || length > kMaxName)
		return "";
	std::string key = clientside ? kClientPrefix : kPrefix;
	for (size_t i = 0; i < length; ++i)
	{
		const char c = name[i];
		key += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
	}
	return key;
}

int32_t ComputeRandomRange(int32_t min, int32_t max, uint32_t draw)
{
	if (max < min)
		std::swap(min, max);
	const uint64_t span = uint64_t(int64_t(max) - int64_t(min)) + 1;
	return int32_t(int64_t(min) + int64_t(draw % span));
}

int32_t ComputeFixedRange(int32_t min, int32_t max, uint32_t draw)
{
	if (max < min)
		std::swap(min, max);
	const uint64_t span = uint64_t(int64_t(max) - int64_t(min));
	// [Doruk-Aksoy] draw / 0xFFFFFFFF of the span, so 0 gives min and 0xFFFFFFFF gives max, as ZScript's frandom reads its [0, 1] real.
	return int32_t(int64_t(min) + int64_t((span * draw) / 0xFFFFFFFFull));
}

std::vector<uint8_t> ComputeEncodeKeys(const std::vector<std::string> &keys)
{
	std::vector<uint8_t> out;
	const auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
	put32(uint32_t(keys.size()));
	for (const std::string &key : keys)
	{
		out.push_back(uint8_t(key.size()));
		out.insert(out.end(), key.begin(), key.end());
	}
	return out;
}

bool ComputeKeyIsOurs(const std::string &key)
{
	for (const char *prefix : { kPrefix, kClientPrefix })
	{
		const size_t length = strlen(prefix);
		if (key.compare(0, length, prefix) == 0)
			return key.size() > length && key.size() - length <= kMaxName;
	}
	return false;
}

bool ComputeDecodeKeys(const uint8_t *data, size_t size, std::vector<std::string> &keys)
{
	keys.clear();
	if (data == nullptr || size < 4)
		return false;
	const uint32_t count = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
	size_t at = 4;
	for (uint32_t i = 0; i < count; ++i)
	{
		if (at >= size)
			return false;
		const size_t length = data[at++];
		if (at + length > size)
			return false;
		std::string key(reinterpret_cast<const char *>(data + at), length);
		at += length;
		if (!ComputeKeyIsOurs(key))
			return false;
		keys.push_back(key);
	}
	return at == size;
}

}} // namespace zx::acsrandom
