// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The standalone scene, driven without GL: the frame's upkeep, the visibility walks, the re-bake of what moved, and the actors in view.

#ifndef ZX_STANDALONEFRAME_H
#define ZX_STANDALONEFRAME_H

class AActor;
struct particle_t;
struct sector_t;

namespace zx { namespace frame {

// [rc4l] What the scene hands back to a renderer still running beside the backend, for what the backend does not derive itself; either may be NULL.
struct SceneHooks
{
	void (*actor)(AActor *thing);                              // [rc4l] an actor in view the backend left (fua_sprites_own off, or the check on)
	void (*particle)(particle_t *particle, sector_t *sector);  // [rc4l] a particle a walk reached that the backend left
};

// [rc4l] The scene for the player's view; false when the world moved under GL's own bake and the caller must derive the scene itself.
bool StandaloneScene(const SceneHooks &hooks);

}} // namespace zx::frame

#endif // ZX_STANDALONEFRAME_H
