// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The textures and buffers a WebGPU device frees itself: a browser frees a released object's memory only when its garbage collector gets to it, which a page resized a few times outruns.

#ifndef ZX_KEPTOBJECTS_COMPUTE_H
#define ZX_KEPTOBJECTS_COMPUTE_H

#include <stddef.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace zx { namespace hwrender {

// [rc4l] Whether a buffer has GPU memory of its own to free: a dynamic one is a range of the device's shared ring, and a staging one is CPU memory.
bool ComputeBufferOwnsMemory(bool dynamic, bool staging);

// [rc4l] Each kept object's API handle and whether it is a texture (else a buffer), with an Owner the glue asks whether anything still holds it.
template <class Owner>
class KeptObjects
{
public:
	// [rc4l] Keeps handle under owner; false for a null handle or one kept already, which the glue then takes no reference on.
	bool Keep(void *handle, bool texture, Owner owner)
	{
		if (handle == nullptr || !m_handles.insert(handle).second) return false;
		m_entries.push_back(Entry{ handle, texture, std::move(owner) });
		return true;
	}

	// [rc4l] Hands free(handle, texture) each kept object whose owner released(owner) says nothing holds any more, and forgets it; the count freed.
	template <class Released, class Free>
	unsigned FreeReleased(Released released, Free free)
	{
		unsigned freed = 0;
		for (size_t i = 0; i < m_entries.size();)
		{
			if (!released(m_entries[i].owner)) { i++; continue; }
			free(m_entries[i].handle, m_entries[i].texture);
			m_handles.erase(m_entries[i].handle);
			m_entries[i] = std::move(m_entries.back());
			m_entries.pop_back();
			freed++;
		}
		return freed;
	}

private:
	struct Entry { void *handle; bool texture; Owner owner; };
	std::vector<Entry> m_entries;
	std::unordered_set<void *> m_handles;
};

}} // namespace zx::hwrender

#endif // ZX_KEPTOBJECTS_COMPUTE_H
