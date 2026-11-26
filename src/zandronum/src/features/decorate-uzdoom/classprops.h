// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_CLASSPROPS_H
#define ZX_CLASSPROPS_H

#include "actor.h"
#include "p_local.h"

// [Doruk-Aksoy] The per-class values of UZDoom's DECORATE properties, read from the class as the defaults they are.
namespace zx { namespace decorateuzdoom {

inline fixed_t MissileChanceMult(AActor *a) { return a->GetClass()->Meta.GetMetaFixed(AMETA_MissileChanceMult, FRACUNIT); }
inline fixed_t SelfDamageFactor(AActor *a) { return a->GetClass()->Meta.GetMetaFixed(AMETA_SelfDamageFactor, FRACUNIT); }
inline fixed_t StealthAlpha(AActor *a) { return a->GetClass()->Meta.GetMetaFixed(AMETA_StealthAlpha, 0); }
inline int FriendlySeeBlocks(AActor *a) { return a->GetClass()->Meta.GetMetaInt(AMETA_FriendlySeeBlocks, 10); }
inline int MinRespawnTics(AActor *a) { return a->GetClass()->Meta.GetMetaInt(AMETA_MinRespawnTics, 0); }
inline int RespawnDice(AActor *a) { return a->GetClass()->Meta.GetMetaInt(AMETA_RespawnDice, 4); }
inline int DefThreshold(AActor *a) { return a->GetClass()->Meta.GetMetaInt(AMETA_DefThreshold, BASETHRESHOLD); }

}} // namespace zx::decorateuzdoom

#endif // ZX_CLASSPROPS_H
