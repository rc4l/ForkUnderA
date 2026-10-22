// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/netrpc_compute.h"

#include <gtest/gtest.h>

using namespace zx::mcp;

TEST(ComputeNetRoleName, EachRoleAndAnUnknownOne)
{
	EXPECT_STREQ("single", ComputeNetRoleName(0));
	EXPECT_STREQ("skirmish", ComputeNetRoleName(1));
	EXPECT_STREQ("client", ComputeNetRoleName(2));
	EXPECT_STREQ("server", ComputeNetRoleName(3));
	EXPECT_STREQ("unknown", ComputeNetRoleName(4));
	EXPECT_STREQ("unknown", ComputeNetRoleName(-1));
}

TEST(ComputeConnectionStateName, FromDisconnectedToActive)
{
	EXPECT_STREQ("disconnected", ComputeConnectionStateName(0));
	EXPECT_STREQ("connecting", ComputeConnectionStateName(1));
	EXPECT_STREQ("authenticating", ComputeConnectionStateName(2));
	EXPECT_STREQ("requesting_snapshot", ComputeConnectionStateName(3));
	EXPECT_STREQ("receiving_snapshot", ComputeConnectionStateName(4));
	EXPECT_STREQ("active", ComputeConnectionStateName(5));
	EXPECT_STREQ("unknown", ComputeConnectionStateName(6));
	EXPECT_STREQ("unknown", ComputeConnectionStateName(-3));
}

TEST(ComputeClientSlotStateName, FromFreeToSpawned)
{
	EXPECT_STREQ("free", ComputeClientSlotStateName(0));
	EXPECT_STREQ("challenge", ComputeClientSlotStateName(1));
	EXPECT_STREQ("connected", ComputeClientSlotStateName(2));
	EXPECT_STREQ("authenticated_outdated_map", ComputeClientSlotStateName(3));
	EXPECT_STREQ("authenticated", ComputeClientSlotStateName(4));
	EXPECT_STREQ("spawned_needs_authentication", ComputeClientSlotStateName(5));
	EXPECT_STREQ("spawned", ComputeClientSlotStateName(6));
	EXPECT_STREQ("unknown", ComputeClientSlotStateName(7));
	EXPECT_STREQ("unknown", ComputeClientSlotStateName(-1));
}

TEST(ComputeNetClientJson, EveryFieldWithTheNameEscaped)
{
	NetClientRow r;
	r.player = 1;
	r.name = "a\"b";
	r.state = 6;
	r.ping = 48;
	r.lastServerTic = 700;
	r.clientTic = 690;
	r.spectating = true;
	EXPECT_EQ("{\"player\":1,\"name\":\"a\\\"b\",\"state\":\"spawned\",\"ping\":48,\"lastServerTic\":700,\"clientTic\":690,\"spectating\":true,\"bot\":false}",
		ComputeNetClientJson(r));
	r.spectating = false;
	r.bot = true;
	EXPECT_NE(std::string::npos, ComputeNetClientJson(r).find("\"spectating\":false,\"bot\":true"));
}

TEST(ComputeWorldActorJson, PositionsToAThousandthAndEveryField)
{
	WorldActorRow r;
	r.netId = 12;
	r.cls = "DoomImp";
	r.x = 1.5;
	r.y = -2.25;
	r.z = 0.0005;
	r.velX = 3;
	r.angle = 90;
	r.health = 60;
	r.sprite = "TROOA";
	EXPECT_EQ("{\"netid\":12,\"c\":\"DoomImp\",\"x\":1.500,\"y\":-2.250,\"z\":0.001,\"vx\":3.000,\"vy\":0.000,\"vz\":0.000,\"angle\":90.000,\"health\":60,\"player\":-1,\"sprite\":\"TROOA\"}",
		ComputeWorldActorJson(r));
}

TEST(ComputeSectorStateJson, HeightsLightColoursWithoutAlphaAndFlats)
{
	SectorStateRow r;
	r.index = 3;
	r.floor = 8;
	r.ceiling = 128.25;
	r.light = 160;
	r.color = 0xff112233u;
	r.fade = 0x00405060u;
	r.desaturation = 12;
	r.floorTex = "FLOOR4_8";
	r.ceilingTex = "CEIL3_5";
	EXPECT_EQ("{\"i\":3,\"floor\":8.000,\"ceiling\":128.250,\"light\":160,\"color\":1122867,\"fade\":4214880,\"desat\":12,\"floorTex\":\"FLOOR4_8\",\"ceilingTex\":\"CEIL3_5\"}",
		ComputeSectorStateJson(r));
}

TEST(ComputeSideStateJson, TexturesLightAndFlags)
{
	SideStateRow r;
	r.index = 7;
	r.top = "STARTAN2";
	r.mid = "-";
	r.bottom = "SW1BRCOM";
	r.light = -16;
	r.flags = 5;
	EXPECT_EQ("{\"i\":7,\"top\":\"STARTAN2\",\"mid\":\"-\",\"bottom\":\"SW1BRCOM\",\"light\":-16,\"flags\":5}", ComputeSideStateJson(r));
}

TEST(ComputePageRange, ClampedToWhatExists)
{
	int b = -1, e = -1;
	ComputePageRange(10, 2, 3, b, e);
	EXPECT_EQ(2, b); EXPECT_EQ(5, e);
	ComputePageRange(10, 8, 5, b, e);
	EXPECT_EQ(8, b); EXPECT_EQ(10, e);
	ComputePageRange(10, -4, 3, b, e);
	EXPECT_EQ(0, b); EXPECT_EQ(3, e);
	ComputePageRange(10, 20, 3, b, e);
	EXPECT_EQ(10, b); EXPECT_EQ(10, e);
	ComputePageRange(10, 4, -1, b, e);
	EXPECT_EQ(4, b); EXPECT_EQ(10, e);
	ComputePageRange(-2, 0, 5, b, e);
	EXPECT_EQ(0, b); EXPECT_EQ(0, e);
}

TEST(ComputeSortActorRows, ByNetworkIdKeepingTiesInOrder)
{
	std::vector<WorldActorRow> rows(4);
	rows[0].netId = 9; rows[1].netId = 2; rows[2].netId = 0; rows[2].cls = "first"; rows[3].netId = 0; rows[3].cls = "second";
	ComputeSortActorRows(rows);
	EXPECT_EQ(0u, rows[0].netId);
	EXPECT_EQ("first", rows[0].cls);
	EXPECT_EQ("second", rows[1].cls);
	EXPECT_EQ(2u, rows[2].netId);
	EXPECT_EQ(9u, rows[3].netId);
}
