// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The fua_ ACS functions for GLDEFS post-process shaders: a script's call, a client receiving what a server's script did, what a joining player is sent, and the level's state in a savegame.

#ifndef ZX_ACSSHADERS_H
#define ZX_ACSSHADERS_H

#include "features/acs-shaders/computation/acsshaders_compute.h"

class AActor;
class FArchive;

namespace zx { namespace acsshaders {

// [Doruk-Aksoy] Runs fua_SetShaderEnabled or a fua_SetShaderUniform for an ACS script whose activator is `activator`, returning the script's result (1 done, 0 not).
int CallShaderFunction(AActor *activator, int function, const int *args, int argCount);

// [Doruk-Aksoy] A server's enable arriving on a client: everyone's or (own) only this client's, going off after `remainingTics` (0 never), its clock already `elapsedTics` in.
void ApplyShaderEnabled(const char *shader, bool own, bool enabled, int remainingTics, int elapsedTics);

// [Doruk-Aksoy] A server's uniform arriving on a client.
void ApplyShaderUniform(const char *shader, const char *uniform, bool own, const UniformValue &value);

// [Doruk-Aksoy] Sends a joining player the level's shader state that reaches them.
void SendShaderStateTo(int player);

// [Doruk-Aksoy] Forgets a leaving player's own shader state.
void DropPlayerShaders(int player);

// [Doruk-Aksoy] Forgets every script's change, as a round reset starts the level afresh on the server and each client.
void ResetLevelShaders();

// [Doruk-Aksoy] The level's shader state in a savegame, from SAVEVER 4526 on.
void SerializeLevelShaders(FArchive &arc);

}} // namespace zx::acsshaders

#endif // ZX_ACSSHADERS_H
