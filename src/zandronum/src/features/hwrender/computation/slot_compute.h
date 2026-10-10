// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A few fixed slots shared out among whatever asks for one in a frame: reflective planes by height, stacked sectors by portal.

#ifndef ZX_SLOT_COMPUTE_H
#define ZX_SLOT_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The slot for a key: the one already holding an equal key, a new one while fewer than maxSlots are used, else -1.
template <class T>
int ComputeSlotFor(T *slots, int &used, int maxSlots, const T &key)
{
	for (int i = 0; i < used; i++)
		if (slots[i] == key) return i;
	if (used >= maxSlots) return -1;
	slots[used] = key;
	return used++;
}

}} // namespace zx::hwrender

#endif // ZX_SLOT_COMPUTE_H
