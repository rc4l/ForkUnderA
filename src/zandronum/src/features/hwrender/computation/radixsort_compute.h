// [Doruk-Aksoy] A stable radix sort, least significant byte first, over keys handed over a byte at a time, which skips every byte all the keys share.
#ifndef ZX_RADIXSORT_COMPUTE_H
#define ZX_RADIXSORT_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] Sorts items by the KeyBytes-byte key byteOf(item, b) gives a byte of (byte 0 the least significant), equal keys kept in their input order, with scratch as the second buffer.
template <int KeyBytes, class T, class ByteOf>
void ComputeRadixSort(std::vector<T> &items, std::vector<T> &scratch, ByteOf byteOf)
{
	const size_t n = items.size();
	if (n < 2) return;
	// [Doruk-Aksoy] Every byte's counts in one pass over the items, since a reorder leaves a byte's counts as they were.
	size_t count[KeyBytes][256] = {};
	for (size_t i = 0; i < n; i++)
		for (int b = 0; b < KeyBytes; b++) count[b][byteOf(items[i], b)]++;
	scratch.resize(n);
	for (int b = 0; b < KeyBytes; b++)
	{
		if (count[b][byteOf(items[0], b)] == n) continue;
		size_t start[256];
		size_t sum = 0;
		for (int v = 0; v < 256; v++) { start[v] = sum; sum += count[b][v]; }
		for (size_t i = 0; i < n; i++) scratch[start[byteOf(items[i], b)]++] = items[i];
		items.swap(scratch);
	}
}

}} // namespace zx::hwrender

#endif
