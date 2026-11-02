// [Doruk-Aksoy] A cache of entries for a list remade each frame in an order that keeps its items' order, drops some and appends new ones at its end, found by walking it beside the list rather than by hashing.
#ifndef ZX_KEPTORDER_COMPUTE_H
#define ZX_KEPTORDER_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace levelmesh {

// [Doruk-Aksoy] Entries in the order their keys came last time, each pass's found ones moved down over the dropped and new ones written where room is.
template <class Entry> struct KeptOrder
{
	std::vector<Entry> entries;
	Entry spare;          // [Doruk-Aksoy] a new key's entry when it came before entries not yet passed, kept for this ask alone
	size_t old = 0;       // [Doruk-Aksoy] entries the pass started with
	size_t from = 0;      // [Doruk-Aksoy] where the next key is looked for among them
	size_t placed = 0;    // [Doruk-Aksoy] entries this pass kept or made, all before from or past old
};

// [Doruk-Aksoy] A pass over this frame's keys begun, every entry of the last one still to be found.
template <class Entry> void ComputeKeptBegin(KeptOrder<Entry> &kept)
{
	kept.old = kept.entries.size();
	kept.from = 0;
	kept.placed = 0;
}

// [Doruk-Aksoy] key's entry for this pass: last pass's, with found set, when keyOf names key within window entries ahead, else one to fill afresh, the spare when no room is free before entries not yet passed.
template <class Entry, class Key, class KeyOf>
Entry &ComputeKeptFind(KeptOrder<Entry> &kept, const Key &key, size_t window, KeyOf keyOf, bool &found)
{
	found = false;
	const size_t end = kept.old - kept.from > window ? kept.from + window : kept.old;
	for (size_t k = kept.from; k < end; k++)
	{
		if (!(keyOf(kept.entries[k]) == key)) continue;
		found = true;
		kept.from = k + 1;
		if (k != kept.placed) kept.entries[kept.placed] = kept.entries[k];
		return kept.entries[kept.placed++];
	}
	if (kept.placed >= kept.from && kept.from < kept.old) return kept.spare;
	if (kept.placed == kept.entries.size()) kept.entries.emplace_back();
	return kept.entries[kept.placed++];
}

// [Doruk-Aksoy] The pass ended, every entry it neither found nor made let go.
template <class Entry> void ComputeKeptEnd(KeptOrder<Entry> &kept)
{
	kept.entries.resize(kept.placed);
	kept.old = kept.from = kept.placed;
}

}} // namespace zx::levelmesh

#endif
