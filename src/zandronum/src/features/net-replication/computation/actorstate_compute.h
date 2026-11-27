// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a server sends its clients of an actor that took damage.

#ifndef ZX_ACTORSTATE_COMPUTE_H
#define ZX_ACTORSTATE_COMPUTE_H

#include <cstdint>
#include <vector>

namespace zx { namespace netreplication {

// [rc4l] Whether a damaged actor's health goes to every client: a non-player still alive, since a player's goes with its own damage command and a death with the death.
bool ComputeSendsWoundedHealth(bool isPlayer, int health);

// [Doruk-Aksoy] Whether a death sends its health on its own: a non-player whose KillThing, which carried it, the snapshot suppresses (legacy suppression 2), so clients' copies read the server's dead health.
bool ComputeSendsDeathHealth(bool isPlayer, bool killSuppressed);

// [rc4l] Add a wounded actor's network id to the tic's pending list once, so a shotgun's seven pellets send one health; false for no id (a server-only actor) or one already pending.
bool ComputeAddPendingHealth(std::vector<int> &pending, int netId);

// [Doruk-Aksoy] The height a death is sent at: a walker killed after stepping onto a higher floor, before the next tic's P_ZMovement raised it there, stands below it, and that raise came after the send, so clients kept a corpse a step low on narrow stairs; one with gravity is sent standing on the floor.
int64_t ComputeSettledDeathZ(int64_t z, int64_t floorz, bool noGravity);

// [Doruk-Aksoy] Whether a non-sliding mover stopped dead by a block sends where it stopped: a non-player that was moving, since a client its block did not stop (a spawn telefrag's corpse, still solid inside the joiner) moved on the velocity it was sent.
bool ComputeSendsBlockedStop(bool isPlayer, bool wasMoving);

// [rc4l] How a server sends a puff: with no net id, with one, or to each client in turn.
enum PuffSend { kPuffNoNetID, kPuffWithNetID, kPuffEachClient };

// [rc4l] A puff in any state but its spawn state goes with no net id, one whose sounds or obituary need it with one, else to each client.
int ComputePuffSend(bool spawnState, bool hitThingSeeSound, bool attackSound, bool temporaryObituary);

// [rc4l] Whether a client is left out of a puff sent to each in turn: the shooter itself, predicting a NONETID puff.
bool ComputeSkipsPuffFor(bool isShooter, bool noNetId, bool clientSidePuffs);

// [Doruk-Aksoy] Whether a joining client is sent an actor's translation: whenever it is not its class's, which a cleared one of a translated class is not.
bool ComputeJoinSendsTranslation(uint32_t translation, uint32_t classTranslation);

// [Doruk-Aksoy] A flag word a client takes from its server: the server's bits, but those in clientOwned the client keeps its own (a player body's RF_INVISIBLE).
uint32_t ComputeClientAppliedFlags(uint32_t current, uint32_t incoming, uint32_t clientOwned);

}} // namespace zx::netreplication

#endif // ZX_ACTORSTATE_COMPUTE_H
