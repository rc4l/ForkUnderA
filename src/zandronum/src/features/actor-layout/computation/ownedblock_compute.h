// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] A block of fields one actor owns alone, made on its first write; an actor without one reads the type's defaults.
#ifndef ZX_OWNEDBLOCK_COMPUTE_H
#define ZX_OWNEDBLOCK_COMPUTE_H

namespace zx { namespace actorlayout {

// [Doruk-Aksoy] The block to read, or one shared set of the type's defaults when there is none.
template<class T> const T &ComputeBlockRead(const T *block)
{
	static const T empty;
	return block != nullptr ? *block : empty;
}

// [Doruk-Aksoy] The block to write, made from the type's defaults the first time.
template<class T> T &ComputeBlockWrite(T *&block)
{
	if (block == nullptr)
		block = new T;
	return *block;
}

// [Doruk-Aksoy] A copy's own block with the other's values, or none when the other had none.
template<class T> T *ComputeBlockCopy(const T *other)
{
	return other != nullptr ? new T(*other) : nullptr;
}

// [Doruk-Aksoy] The other's values into this block, which is freed when the other has none.
template<class T> void ComputeBlockAssign(T *&mine, const T *other)
{
	if (other != nullptr)
		ComputeBlockWrite(mine) = *other;
	else
	{
		delete mine;
		mine = nullptr;
	}
}

}} // namespace zx::actorlayout

#endif
