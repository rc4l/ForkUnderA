// [rc4l] TDeletingArray, whose DeleteAndClear frees what one start made before a restart (wad_reload) makes it again.
#include "tarray.h"

#include <gtest/gtest.h>
#include <cstdlib>

// [rc4l] The engine's m_alloc reaches its garbage collector's accounting, so the tests allocate through the C heap the same way.
#if defined(_DEBUG)
void *M_Malloc_Dbg(size_t size, const char *, int) { return malloc(size); }
void *M_Realloc_Dbg(void *memblock, size_t size, const char *, int) { return realloc(memblock, size); }
#else
void *M_Malloc(size_t size) { return malloc(size); }
void *M_Realloc(void *memblock, size_t size) { return realloc(memblock, size); }
#endif
void M_Free(void *memblock) { free(memblock); }

namespace {

// [rc4l] An element that counts its deletions.
struct Counted
{
	explicit Counted(int &deleted) : deleted(deleted) {}
	~Counted() { deleted++; }
	int &deleted;
};

} // namespace

// [rc4l] A restart refills the light lists and weapon order entries, so what the last start made is deleted and the array is usable again.
TEST(TDeletingArray, DeleteAndClearFreesEveryElementAndEmptiesIt)
{
	int deleted = 0;
	TDeletingArray<Counted *> array;
	for (int i = 0; i < 20; i++) array.Push(new Counted(deleted));
	array.Push(NULL);
	array.DeleteAndClear();
	EXPECT_EQ(deleted, 20);
	EXPECT_EQ(array.Size(), 0u);
	array.DeleteAndClear();
	array.Push(new Counted(deleted));
	EXPECT_EQ(array.Size(), 1u);
}

TEST(TDeletingArray, ItsDestructorStillDeletesWhatItHolds)
{
	int deleted = 0;
	{
		TDeletingArray<Counted *> array;
		array.Push(new Counted(deleted));
		array.Push(new Counted(deleted));
	}
	EXPECT_EQ(deleted, 2);
}
