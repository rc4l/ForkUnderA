// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The one declaration of what the renderer reads of the game, each line a member of a map structure's state (SectorState, SideState, LineState, VertexState in r_defs.h) or of an actor record, from which come the copy's comparison and fill, fua_copy_check, the size check that keeps this list and the structs in step, and the boundary tripwire's patterns for reading a member live.

// [rc4l] ZX_COPY_FIELD(kind, member, group): group is what a change means to the renderer's caches, Shape (walls re-baked), Look (re-checked, re-baked only if the picture changed) or Plane (a plane's placement re-bakes its flats, anything else its walls), and the list has no include guard since each use expands it with the lines it reads defined.
// [rc4l] ZX_ACTOR_FIELD(member) is an actor member each actor record copies, ZX_ACTOR_DERIVED(type, member) a value the fill works out from the game for it, ZX_PLAYER_*, ZX_LEVEL_*, ZX_DECAL_FIELD and ZX_FLATDECAL_FIELD the same for a player record, the level's, a decal's and a floor or ceiling mark's own, and a line kind a use leaves undefined expands to nothing.

#ifndef ZX_COPY_FIELD
#define ZX_COPY_FIELD(kind, member, group)
#define ZX_COPY_FIELD_DEFAULTED
#endif
#ifndef ZX_ACTOR_FIELD
#define ZX_ACTOR_FIELD(member)
#define ZX_ACTOR_FIELD_DEFAULTED
#endif
#ifndef ZX_ACTOR_DERIVED
#define ZX_ACTOR_DERIVED(type, member)
#define ZX_ACTOR_DERIVED_DEFAULTED
#endif
#ifndef ZX_PLAYER_FIELD
#define ZX_PLAYER_FIELD(member)
#define ZX_PLAYER_FIELD_DEFAULTED
#endif
#ifndef ZX_PLAYER_DERIVED
#define ZX_PLAYER_DERIVED(type, member)
#define ZX_PLAYER_DERIVED_DEFAULTED
#endif
#ifndef ZX_DECAL_FIELD
#define ZX_DECAL_FIELD(member)
#define ZX_DECAL_FIELD_DEFAULTED
#endif
#ifndef ZX_FLATDECAL_FIELD
#define ZX_FLATDECAL_FIELD(member)
#define ZX_FLATDECAL_FIELD_DEFAULTED
#endif
#ifndef ZX_LEVEL_FIELD
#define ZX_LEVEL_FIELD(member)
#define ZX_LEVEL_FIELD_DEFAULTED
#endif
#ifndef ZX_LEVEL_DERIVED
#define ZX_LEVEL_DERIVED(type, member)
#define ZX_LEVEL_DERIVED_DEFAULTED
#endif

ZX_COPY_FIELD(sector, planes, Plane)
ZX_COPY_FIELD(sector, floorplane, Shape)
ZX_COPY_FIELD(sector, ceilingplane, Shape)
ZX_COPY_FIELD(sector, ColorMap, Look)
ZX_COPY_FIELD(sector, special, Look)
ZX_COPY_FIELD(sector, lightlevel, Look)
ZX_COPY_FIELD(sector, MoreFlags, Look)
ZX_COPY_FIELD(sector, Flags, Look)
ZX_COPY_FIELD(sector, reflect, Look)
ZX_COPY_FIELD(side, textures, Shape)
ZX_COPY_FIELD(side, Light, Shape)
ZX_COPY_FIELD(side, Flags, Shape)
ZX_COPY_FIELD(line, flags, Shape)
ZX_COPY_FIELD(line, special, Shape)
ZX_COPY_FIELD(line, Alpha, Shape)
ZX_COPY_FIELD(line, args, Shape)
ZX_COPY_FIELD(vertex, x, Shape)
ZX_COPY_FIELD(vertex, y, Shape)

ZX_ACTOR_FIELD(x)
ZX_ACTOR_FIELD(y)
ZX_ACTOR_FIELD(z)
ZX_ACTOR_FIELD(PrevX)
ZX_ACTOR_FIELD(PrevY)
ZX_ACTOR_FIELD(PrevZ)
ZX_ACTOR_FIELD(velx)
ZX_ACTOR_FIELD(vely)
ZX_ACTOR_FIELD(velz)
ZX_ACTOR_FIELD(angle)
ZX_ACTOR_FIELD(PrevAngle)
ZX_ACTOR_FIELD(pitch)
ZX_ACTOR_FIELD(roll)
ZX_ACTOR_FIELD(PrevRoll)
ZX_ACTOR_FIELD(sprite)
ZX_ACTOR_FIELD(frame)
ZX_ACTOR_FIELD(scaleX)
ZX_ACTOR_FIELD(scaleY)
ZX_ACTOR_FIELD(RenderStyle)
ZX_ACTOR_FIELD(renderflags)
ZX_ACTOR_FIELD(alpha)
ZX_ACTOR_FIELD(fillcolor)
ZX_ACTOR_FIELD(Translation)
ZX_ACTOR_FIELD(flags)
ZX_ACTOR_FIELD(flags2)
ZX_ACTOR_FIELD(flags3)
ZX_ACTOR_FIELD(flags5)
ZX_ACTOR_FIELD(state)
ZX_ACTOR_FIELD(tics)
ZX_ACTOR_FIELD(radius)
ZX_ACTOR_FIELD(height)
ZX_ACTOR_FIELD(floorclip)
ZX_ACTOR_FIELD(floorz)
ZX_ACTOR_FIELD(ceilingz)
ZX_ACTOR_FIELD(Speed)
ZX_ACTOR_FIELD(Sector)
ZX_ACTOR_FIELD(subsector)
ZX_ACTOR_FIELD(reactiontime)
ZX_ACTOR_FIELD(health)
ZX_ACTOR_DERIVED(const void *, who)          // [rc4l] the live actor, compared and never read
ZX_ACTOR_DERIVED(unsigned int, id)           // [rc4l] its serial, never reused, which renderer caches key on
ZX_ACTOR_DERIVED(const PClass *, type)
ZX_ACTOR_DERIVED(const void *, target)       // [rc4l] who its target is
ZX_ACTOR_DERIVED(int, nextInSector)          // [rc4l] the next record in its sector's list, -1 at the end
ZX_ACTOR_DERIVED(int, shownSprite)           // [rc4l] the sprite and scale after a player's skin and crouch
ZX_ACTOR_DERIVED(fixed_t, shownScaleX)
ZX_ACTOR_DERIVED(fixed_t, shownScaleY)
ZX_ACTOR_DERIVED(bool, visible)              // [rc4l] IsVisibleToPlayer
ZX_ACTOR_DERIVED(bool, isPlayer)
ZX_ACTOR_DERIVED(int, playerIndex)           // [rc4l] its player's number, -1 for none
ZX_ACTOR_DERIVED(bool, inventory)
ZX_ACTOR_DERIVED(bool, cameraIcon)           // [rc4l] the camera player's icon, hidden outside the chase camera
ZX_ACTOR_DERIVED(int, bobTic)                // [rc4l] FloatBobPhase plus the level's tic
ZX_ACTOR_DERIVED(fixed_t, attackRadius)
ZX_ACTOR_DERIVED(fixed_t, attackHeight)
ZX_ACTOR_DERIVED(bool, playerBody)           // [rc4l] a player's own body, the only actor its floor tints
ZX_ACTOR_DERIVED(bool, tintActive)           // [rc4l] its floor hurts it and nothing protects it
ZX_ACTOR_DERIVED(FTextureID, tintTexture)    // [rc4l] the floor whose colour tints it

ZX_PLAYER_FIELD(viewz)
ZX_PLAYER_FIELD(playerstate)
ZX_PLAYER_FIELD(cheats)
ZX_PLAYER_FIELD(bSpectating)
ZX_PLAYER_FIELD(extralight)
ZX_PLAYER_FIELD(fixedcolormap)
ZX_PLAYER_FIELD(fixedlightlevel)
ZX_PLAYER_FIELD(DesiredFOV)
ZX_PLAYER_FIELD(FOV)
ZX_PLAYER_FIELD(centering)
ZX_PLAYER_FIELD(MinPitch)
ZX_PLAYER_FIELD(MaxPitch)
ZX_PLAYER_FIELD(timefreezer)
ZX_PLAYER_FIELD(BlendR)
ZX_PLAYER_FIELD(BlendG)
ZX_PLAYER_FIELD(BlendB)
ZX_PLAYER_FIELD(BlendA)
ZX_PLAYER_DERIVED(const void *, who)         // [rc4l] the live player, compared and never read
ZX_PLAYER_DERIVED(int, number)
ZX_PLAYER_DERIVED(int, mo)                   // [rc4l] its body's record, -1 for none
ZX_PLAYER_DERIVED(int, camera)               // [rc4l] the record of whose eyes it sees through, its body's when it names none
ZX_PLAYER_DERIVED(int, lightPower)           // [rc4l] a torch's or light amp's, as FixedColormap reads the inventory
ZX_PLAYER_DERIVED(bool, hasWeapon)
ZX_PLAYER_DERIVED(float, weaponFOVScale)
ZX_PLAYER_DERIVED(const PClass *, weaponType)
ZX_PLAYER_DERIVED(fixed_t, weaponYAdjust)
ZX_PLAYER_DERIVED(bool, hudModel)            // [rc4l] MODELDEF draws its weapon as a HUD model
ZX_PLAYER_DERIVED(FWeaponBobIn, bobIn)       // [rc4l] what P_BobWeapon reads of it
ZX_PLAYER_DERIVED(std::vector<pspdef_t>, layers)   // [rc4l] its weapon's layers, in draw order
ZX_PLAYER_DERIVED(zx::gamecopy::ScreenBlendLayers, screenBlend)   // [rc4l] what V_AddPlayerBlend adds to the screen

ZX_DECAL_FIELD(LeftDistance)
ZX_DECAL_FIELD(Z)
ZX_DECAL_FIELD(ScaleX)
ZX_DECAL_FIELD(ScaleY)
ZX_DECAL_FIELD(Alpha)
ZX_DECAL_FIELD(AlphaColor)
ZX_DECAL_FIELD(Translation)
ZX_DECAL_FIELD(PicNum)
ZX_DECAL_FIELD(RenderFlags)
ZX_DECAL_FIELD(RenderStyle)
ZX_DECAL_FIELD(Sector)

ZX_FLATDECAL_FIELD(ContactX)
ZX_FLATDECAL_FIELD(ContactY)
ZX_FLATDECAL_FIELD(ContactZ)
ZX_FLATDECAL_FIELD(Normal)
ZX_FLATDECAL_FIELD(Travel)
ZX_FLATDECAL_FIELD(AnchorSector)
ZX_FLATDECAL_FIELD(AnchorPlane)
ZX_FLATDECAL_FIELD(AnchorOffset)

ZX_LEVEL_FIELD(time)
ZX_LEVEL_FIELD(maptime)
ZX_LEVEL_FIELD(flags)
ZX_LEVEL_FIELD(flags2)
ZX_LEVEL_FIELD(skyspeed1)
ZX_LEVEL_FIELD(skyspeed2)
ZX_LEVEL_FIELD(fadeto)
ZX_LEVEL_FIELD(outsidefog)
ZX_LEVEL_DERIVED(FTextureID, sky1)           // [rc4l] sky1texture, which ChangeSky sets
ZX_LEVEL_DERIVED(FTextureID, sky2)
ZX_LEVEL_DERIVED(bool, endLevelDelay)        // [rc4l] a game mode's pause before the next level, which holds the view still

#ifdef ZX_COPY_FIELD_DEFAULTED
#undef ZX_COPY_FIELD
#undef ZX_COPY_FIELD_DEFAULTED
#endif
#ifdef ZX_ACTOR_FIELD_DEFAULTED
#undef ZX_ACTOR_FIELD
#undef ZX_ACTOR_FIELD_DEFAULTED
#endif
#ifdef ZX_ACTOR_DERIVED_DEFAULTED
#undef ZX_ACTOR_DERIVED
#undef ZX_ACTOR_DERIVED_DEFAULTED
#endif
#ifdef ZX_PLAYER_FIELD_DEFAULTED
#undef ZX_PLAYER_FIELD
#undef ZX_PLAYER_FIELD_DEFAULTED
#endif
#ifdef ZX_PLAYER_DERIVED_DEFAULTED
#undef ZX_PLAYER_DERIVED
#undef ZX_PLAYER_DERIVED_DEFAULTED
#endif
#ifdef ZX_DECAL_FIELD_DEFAULTED
#undef ZX_DECAL_FIELD
#undef ZX_DECAL_FIELD_DEFAULTED
#endif
#ifdef ZX_FLATDECAL_FIELD_DEFAULTED
#undef ZX_FLATDECAL_FIELD
#undef ZX_FLATDECAL_FIELD_DEFAULTED
#endif
#ifdef ZX_LEVEL_FIELD_DEFAULTED
#undef ZX_LEVEL_FIELD
#undef ZX_LEVEL_FIELD_DEFAULTED
#endif
#ifdef ZX_LEVEL_DERIVED_DEFAULTED
#undef ZX_LEVEL_DERIVED
#undef ZX_LEVEL_DERIVED_DEFAULTED
#endif
