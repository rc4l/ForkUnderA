// [rc4l] Covers every branch of the key-table inversion.
#include <gtest/gtest.h>

#include "keymap_compute.h"

TEST(KeyMap, InvertsAMappedTable)
{
	const unsigned short dik[4] = { 0, 40, 41, 5 };
	unsigned char out[64];
	ComputeInvertKeyTable(dik, 4, out, 64);

	EXPECT_EQ(1, out[40]);
	EXPECT_EQ(2, out[41]);
	EXPECT_EQ(3, out[5]);
}

TEST(KeyMap, UnmappedScancodesStayZero)
{
	const unsigned short dik[2] = { 0, 7 };
	unsigned char out[16];
	ComputeInvertKeyTable(dik, 2, out, 16);

	EXPECT_EQ(1, out[7]);
	for (int i = 0; i < 16; ++i)
	{
		if (i != 7) EXPECT_EQ(0, out[i]) << "scancode " << i;
	}
}

TEST(KeyMap, LowestDIKWinsWhenTwoMapToTheSameScancode)
{
	// [rc4l] Both DIK 2 and DIK 9 claim scancode 30; the lower code must win.
	unsigned short dik[10] = { 0, 0, 30, 0, 0, 0, 0, 0, 0, 30 };
	unsigned char out[64];
	ComputeInvertKeyTable(dik, 10, out, 64);

	EXPECT_EQ(2, out[30]);
}

TEST(KeyMap, ScancodesBeyondTheOutputAreIgnored)
{
	// [rc4l] A scancode past the destination table must not write out of bounds.
	const unsigned short dik[2] = { 0, 500 };
	unsigned char out[8];
	ComputeInvertKeyTable(dik, 2, out, 8);

	for (int i = 0; i < 8; ++i) EXPECT_EQ(0, out[i]);
}

TEST(KeyMap, NullAndEmptyInputsAreRejected)
{
	unsigned char out[4] = { 9, 9, 9, 9 };
	ComputeInvertKeyTable(nullptr, 0, nullptr, 4);
	ComputeInvertKeyTable(nullptr, 0, out, 0);
	EXPECT_EQ(9, out[0]);

	// [rc4l] A null source still clears the destination.
	ComputeInvertKeyTable(nullptr, 3, out, 4);
	for (int i = 0; i < 4; ++i) EXPECT_EQ(0, out[i]);
}
