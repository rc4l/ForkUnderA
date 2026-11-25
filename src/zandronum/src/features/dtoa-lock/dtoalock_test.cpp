// SPDX-License-Identifier: GPL-3.0-or-later

#include "features/dtoa-lock/dtoalock.h"
#include "gdtoa.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

// [MGOOOOOO] One conversion as FString's %g makes it (mode 2, 6 digits) or a large number's shortest form (mode 0), with its exponent and sign.
struct Converted
{
	std::string digits;
	int decpt;
	int sign;
	bool operator== (const Converted &o) const { return digits == o.digits && decpt == o.decpt && sign == o.sign; }
};

Converted Convert(double value, int mode, int ndigits)
{
	Converted c;
	char *end;
	char *s = dtoa(value, mode, ndigits, &c.decpt, &c.sign, &end);
	c.digits.assign(s, end);
	freedtoa(s);
	return c;
}

struct Case
{
	double value;
	int mode;
	int ndigits;
};

// [MGOOOOOO] Fixed-point values as ACS prints them, and large ones that take the powers-of-five path.
std::vector<Case> Cases()
{
	std::vector<Case> out;
	for (int i = 0; i < 48; i++)
	{
		out.push_back({ (i * 52429 - 1000000) / 65536.0, 2, 6 });
		out.push_back({ (i + 1) * 1.0e250 / 3.0, 0, 0 });
	}
	return out;
}

} // namespace

// [MGOOOOOO] The game thread formats numbers while the main thread does, so every thread must get its own digits back.
TEST(DtoaLock, ConversionsOnManyThreadsMatchOneThread)
{
	const std::vector<Case> cases = Cases();
	std::vector<Converted> expected;
	for (const Case &c : cases) expected.push_back(Convert(c.value, c.mode, c.ndigits));

	std::atomic<int> wrong{ 0 };
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; t++)
	{
		threads.emplace_back([&, t]
		{
			for (int i = 0; i < 40000; i++)
			{
				const size_t k = (size_t)(t * 7 + i) % cases.size();
				if (!(Convert(cases[k].value, cases[k].mode, cases[k].ndigits) == expected[k])) wrong++;
			}
		});
	}
	for (std::thread &th : threads) th.join();
	EXPECT_EQ(wrong.load(), 0);
}

// [MGOOOOOO] A thread taking a held lock waits for its release, and the other lock stays free meanwhile.
TEST(DtoaLock, ASecondThreadWaitsForTheHolder)
{
	fua_dtoa_lock(0);
	std::atomic<bool> got{ false };
	std::thread other([&]
	{
		fua_dtoa_lock(0);
		got = true;
		fua_dtoa_unlock(0);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	EXPECT_FALSE(got.load());
	fua_dtoa_lock(1);
	fua_dtoa_unlock(1);
	fua_dtoa_unlock(0);
	other.join();
	EXPECT_TRUE(got.load());
}
