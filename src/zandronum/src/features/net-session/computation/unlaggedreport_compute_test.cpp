// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/unlaggedreport_compute.h"

#include <gtest/gtest.h>

using namespace zx::netsession;

TEST(ComputeLogShot, NumbersShotsAndKeepsTheMostRecent)
{
	ShotLog log;
	for (int i = 0; i < kShotsKept + 5; i++)
	{
		UnlaggedShot s;
		s.tic = i;
		ComputeLogShot(log, s);
	}
	ASSERT_EQ((size_t)kShotsKept, log.shots.size());
	EXPECT_EQ(6, log.shots.front().number);
	EXPECT_EQ(5, log.shots.front().tic);
	EXPECT_EQ(kShotsKept + 5, log.shots.back().number);
	EXPECT_EQ(kShotsKept + 6, log.next);
}

TEST(ComputeShotJson, TheShotAndEachPlayerWhereTheTraceMetThemAndWhereTheyWere)
{
	UnlaggedShot s;
	s.number = 3;
	s.shooter = 0;
	s.tic = 700;
	s.rewindTic = 696;
	s.hitPlayer = 1;
	ShotPlayer p;
	p.player = 1;
	p.x = 10; p.y = -20.5; p.z = 0;
	p.presentX = 14; p.presentY = -20.5; p.presentZ = 0;
	s.players.push_back(p);
	p.player = 2;
	s.players.push_back(p);
	EXPECT_EQ("{\"n\":3,\"shooter\":0,\"tic\":700,\"rewind\":696,\"hit\":1,\"players\":["
		"{\"p\":1,\"x\":10.000,\"y\":-20.500,\"z\":0.000,\"px\":14.000,\"py\":-20.500,\"pz\":0.000},"
		"{\"p\":2,\"x\":10.000,\"y\":-20.500,\"z\":0.000,\"px\":14.000,\"py\":-20.500,\"pz\":0.000}]}", ComputeShotJson(s));
}

TEST(ComputeShotLogJson, TheRoleAndEveryShot)
{
	ShotLog log;
	EXPECT_EQ("{\"role\":\"server\",\"shots\":[]}", ComputeShotLogJson(log, "server"));
	ComputeLogShot(log, UnlaggedShot());
	ComputeLogShot(log, UnlaggedShot());
	const std::string json = ComputeShotLogJson(log, "client");
	EXPECT_EQ(0u, json.find("{\"role\":\"client\",\"shots\":[{\"n\":1,"));
	EXPECT_NE(std::string::npos, json.find("]},{\"n\":2,"));
}
