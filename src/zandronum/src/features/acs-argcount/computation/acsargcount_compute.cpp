// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-argcount/computation/acsargcount_compute.h"

#include <algorithm>

namespace zx { namespace acsargcount {

// [Doruk-Aksoy] Upstream's MIN_ARG_COUNT, else the minimum zspecial.acs declares, each lowered to the fewest that ACC or BCC lets a script pass.
const FunctionArgs kFunctionArgs[] = {
	{ 1, 2, "GetLineUDMFInt" },
	{ 2, 2, "GetLineUDMFFixed" },
	{ 3, 2, "GetThingUDMFInt" },
	{ 4, 2, "GetThingUDMFFixed" },
	{ 5, 2, "GetSectorUDMFInt" },
	{ 6, 2, "GetSectorUDMFFixed" },
	{ 7, 3, "GetSideUDMFInt" },
	{ 8, 3, "GetSideUDMFFixed" },
	{ 9, 1, "GetActorVelX" },
	{ 10, 1, "GetActorVelY" },
	{ 11, 1, "GetActorVelZ" },
	{ 12, 1, "SetActivator" },
	{ 13, 1, "SetActivatorToTarget" },
	{ 14, 1, "GetActorViewHeight" },
	{ 15, 2, "GetChar" },
	{ 16, 1, "GetAirSupply" },
	{ 17, 2, "SetAirSupply" },
	{ 18, 2, "SetSkyScrollSpeed" },
	{ 19, 2, "GetArmorType" },
	{ 20, 2, "SpawnSpotForced" },
	{ 21, 2, "SpawnSpotFacingForced" },
	{ 22, 3, "CheckActorProperty" },
	{ 23, 6, "SetActorVelocity" },
	{ 24, 3, "SetUserVariable" },
	{ 25, 2, "GetUserVariable" },
	{ 26, 6, "Radius_Quake2" },
	{ 27, 2, "CheckActorClass" },
	{ 28, 4, "SetUserArray" },
	{ 29, 3, "GetUserArray" },
	{ 30, 2, "SoundSequenceOnActor" },
	{ 31, 3, "SoundSequenceOnSector" },
	{ 32, 2, "SoundSequenceOnPolyobj" },
	{ 33, 1, "GetPolyobjX" },
	{ 34, 1, "GetPolyobjY" },
	{ 35, 3, "CheckSight" },
	{ 36, 4, "SpawnForced" },
	{ 37, 2, "AnnouncerSound" },
	{ 38, 2, "SetPointer" },
	{ 39, 1, "ACS_NamedExecute" },
	{ 40, 1, "ACS_NamedSuspend" },
	{ 41, 1, "ACS_NamedTerminate" },
	{ 42, 1, "ACS_NamedLockedExecute" },
	{ 43, 1, "ACS_NamedLockedExecuteDoor" },
	{ 44, 1, "ACS_NamedExecuteWithResult" },
	{ 45, 1, "ACS_NamedExecuteAlways" },
	{ 47, 1, "IsTIDUsed" },
	{ 48, 1, "Sqrt" },
	{ 49, 1, "FixedSqrt" },
	{ 50, 2, "VectorLength" },
	{ 53, 2, "SetCVar" },
	{ 54, 2, "GetUserCVar" },
	{ 55, 3, "SetUserCVar" },
	{ 56, 1, "GetCVarString" },
	{ 57, 2, "SetCVarString" },
	{ 58, 2, "GetUserCVarString" },
	{ 59, 3, "SetUserCVarString" },
	{ 60, 4, "LineAttack" },
	{ 61, 2, "PlaySound" },
	{ 62, 1, "StopSound" },
	{ 63, 2, "strcmp" },
	{ 64, 2, "stricmp" },
	{ 65, 2, "StrLeft" },
	{ 66, 2, "StrRight" },
	{ 67, 3, "StrMid" },
	{ 68, 1, "GetActorClass" },
	{ 70, 3, "SoundVolume" },
	{ 71, 2, "PlayActorSound" },
	{ 72, 2, "SpawnDecal" },
	{ 73, 1, "CheckFont" },
	{ 74, 2, "DropItem" },
	{ 75, 2, "CheckFlag" },
	{ 76, 2, "SetLineActivation" },
	{ 77, 1, "GetLineActivation" },
	{ 78, 2, "GetActorPowerupTics" },
	{ 79, 2, "ChangeActorAngle" },
	{ 80, 2, "ChangeActorPitch" },
	{ 81, 1, "GetArmorInfo" },
	{ 82, 2, "DropInventory" },
	{ 83, 5, "PickActor" },
	{ 84, 2, "IsPointerEqual" },
	{ 85, 1, "CanRaiseActor" },
	{ 86, 3, "SetActorTeleFog" },
	{ 87, 1, "SwapActorTeleFog" },
	{ 88, 2, "SetActorRoll" },
	{ 89, 2, "ChangeActorRoll" },
	{ 90, 1, "GetActorRoll" },
	{ 91, 8, "QuakeEx" },
	{ 92, 6, "Warp" },
	{ 97, 1, "SetMusicVolume" },
	{ 101, 1, "PlayerIsSpectator" },
	{ 103, 2, "GetTeamProperty" },
	{ 104, 1, "GetPlayerLivesLeft" },
	{ 105, 2, "SetPlayerLivesLeft" },
	{ 106, 2, "ForceToSpectate" },
	{ 108, 3, "SetDBEntry" },
	{ 109, 2, "GetDBEntry" },
	{ 110, 3, "SetDBEntryString" },
	{ 111, 2, "GetDBEntryString" },
	{ 112, 3, "IncrementDBEntry" },
	{ 113, 1, "PlayerIsLoggedIn" },
	{ 114, 1, "GetPlayerAccountName" },
	{ 115, 4, "SortDBEntries" },
	{ 116, 1, "CountDBResults" },
	{ 117, 1, "FreeDBResults" },
	{ 118, 2, "GetDBResultKeyString" },
	{ 119, 2, "GetDBResultValueString" },
	{ 120, 2, "GetDBResultValue" },
	{ 121, 3, "GetDBEntryRank" },
	{ 122, 1, "RequestScriptPuke" },
	{ 125, 1, "GetDBEntries" },
	{ 126, 1, "NamedRequestScriptPuke" },
	{ 128, 2, "GetTimeProperty" },
	{ 129, 2, "Strftime" },
	{ 130, 2, "SetDeadSpectator" },
	{ 131, 1, "SetActivatorToPlayer" },
	{ 132, 1, "SetCurrentGamemode" },
	{ 134, 2, "SetGamemodeLimit" },
	{ 135, 3, "SetPlayerClass" },
	{ 136, 2, "SetPlayerChasecam" },
	{ 137, 1, "GetPlayerChasecam" },
	{ 138, 3, "SetPlayerScore" },
	{ 139, 2, "GetPlayerScore" },
	{ 144, 2, "ExecuteClientScript" },
	{ 145, 2, "NamedExecuteClientScript" },
	{ 146, 2, "SendNetworkString" },
	{ 147, 2, "NamedSendNetworkString" },
	{ 148, 2, "GetChatMessage" },
	{ 150, 2, "GetMapRotationInfo" },
	{ 153, 2, "GetActorSectorLocation" },
	{ 154, 3, "ChangeTeamScore" },
	{ 155, 2, "SetGameplaySetting" },
	{ 156, 3, "SetCustomPlayerValue" },
	{ 157, 2, "GetCustomPlayerValue" },
	{ 158, 2, "ResetCustomDataToDefault" },
	{ 159, 1, "LumpOpen" },
	{ 160, 2, "LumpRead" },
	{ 161, 2, "LumpReadString" },
	{ 162, 3, "LumpReadLocal" },
	{ 163, 3, "LumpReadModule" },
	{ 164, 3, "LumpReadHub" },
	{ 165, 3, "LumpReadGlobal" },
	{ 166, 2, "LumpGetInfo" },
	{ 167, 1, "LumpClose" },
	{ 170, 1, "OpenMenu" },
	{ 172, 2, "BanFromGame" },
	{ 173, 1, "GetPlayerStatus" },
	{ 174, 2, "SetPlayerWeaponZoomFactor" },
	{ 175, 2, "SetPlayerSkin" },
	{ 176, 2, "GetPlayerSkin" },
	{ 177, 2, "GetPlayerCountry" },
	{ 178, 2, "SetNextMapPosition" },
	{ 179, 3, "GivePlayerMedal" },
	{ 180, 1, "GetPlayerJoinQueuePosition" },
	{ 181, 1, "SkipJoinQueue" },
	{ 182, 2, "GetControlPointInfo" },
	{ 183, 3, "SetControlPointInfo" },
	{ 184, 2, "GetSkinProperty" },
	{ 185, 2, "IsPlayerContestingControlPoint" },
	{ 204, 1, "GetActorFloorTexture" },
	{ 19620, 1, "GetTeamScore" },
};
const int kFunctionArgsCount = int(sizeof(kFunctionArgs) / sizeof(kFunctionArgs[0]));

namespace {

// [Doruk-Aksoy] Indices below this, which hold every function but ZDaemon's, are looked up directly, since CallFunction asks on every call.
const int kDirect = 256;

struct DirectTable
{
	const FunctionArgs *entries[kDirect];

	DirectTable()
	{
		for (int i = 0; i < kDirect; i++) entries[i] = nullptr;
		for (int i = 0; i < kFunctionArgsCount; i++)
		{
			if (kFunctionArgs[i].index < kDirect) entries[kFunctionArgs[i].index] = &kFunctionArgs[i];
		}
	}
};

} // namespace

const FunctionArgs *ComputeFunctionArgs(int funcIndex)
{
	if (funcIndex < 0) return nullptr;
	if (funcIndex < kDirect)
	{
		static const DirectTable direct;
		return direct.entries[funcIndex];
	}
	const FunctionArgs *end = kFunctionArgs + kFunctionArgsCount;
	const FunctionArgs *it = std::lower_bound(kFunctionArgs, end, funcIndex,
		[](const FunctionArgs &entry, int index) { return entry.index < index; });
	return it != end && it->index == funcIndex ? it : nullptr;
}

const FunctionArgs *ComputeShortCall(int funcIndex, int argCount)
{
	const FunctionArgs *entry = ComputeFunctionArgs(funcIndex);
	return entry != nullptr && argCount < entry->needed ? entry : nullptr;
}

}} // namespace zx::acsargcount
