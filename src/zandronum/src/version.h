/*
** version.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2007 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#ifndef __VERSION_H__
#define __VERSION_H__

const char *GetGitDescription();
const char *GetGitHash();
const char *GetGitTime();
const char *GetVersionString();
// [rc4l] The full `git describe` of this build ("v0.1.19-29-gde55d35"), i.e. OUR version rather
// than Zandronum's. Separate from GetGitDescription(), which returns the 12-char hash the crash
// reporter uses to name symbol assets -- changing that would break crash symbolication.
const char *GetFuaDescribe();
// [BB]
const char *GetVersionStringRev();
// [rc4l] This engine's own release tag ("v0.1.29"), as opposed to the Zandronum version above.
const char *GetFuaVersionTag();
int GetRevisionNumber();

/** Lots of different version numbers **/

#define GAME_MAJOR_VERSION 3
#define GAME_MINOR_VERSION 1
// [rc4l] Our release, not the Zandronum release we forked from. This is the string a client sends on
// connect and a server rejects it over, so builds that are not interchangeable must not claim to be.
// Set by src/zandronum/CMakeLists.txt; the fallback is for builds that bypass our CMake.
#ifndef FUA_GAMEVER_STRING
#define FUA_GAMEVER_STRING "3.2.1"
#endif
#define GAMEVER_STRING FUA_GAMEVER_STRING
#define DOTVERSIONSTR GAMEVER_STRING
#define VERSIONSTR DOTVERSIONSTR

// [BB] The version string that includes revision / compatibility data.
#define DOTVERSIONSTR_REV DOTVERSIONSTR "-r" HG_TIME

// [BC] What version of ZDoom is this based off of?
#define	ZDOOMVERSIONSTR		"2.8pre-441-g458e1b1"

/** Release code stuff */

// Please maintain the existing structure as much as possible, because it's
// used in communicating between servers and clients of different versions.
#define BUILD_OTHER			0
#define BUILD_RELEASE		1
#define BUILD_INTERNAL		2
#define BUILD_PRIVATE		3

// [RC] Release code ID for this build.
#define BUILD_ID			BUILD_RELEASE
#define BUILD_ID_STR		"Release" // Used in the exe's metadata.

// Version identifier for network games.
// Bump it every time you do a release unless you're certain you
// didn't change anything that will affect network protocol.
// 003 = 0.97c2
// 004 = 0.97c3
// 005 = 0.97d-beta4
// 006 = 0.97d-beta4.2
// 007 = 0.97d-RC9
// [BB] Use the revision number to automatically make builds from
// different revisions incompatible. Skulltag only uses one byte
// to transfer NETGAMEVERSION, so we need to limit its value to [0,255].
#define NETGAMEVERSION (GetRevisionNumber() % 256)

// Version stored in the ini's [LastRun] section.
// Bump it if you made some configuration change that you want to
// be able to migrate in FGameConfigFile::DoGlobalSetup().
#define LASTRUNVERSION "210"

// [TP] Same as above except for Zandronum-specific changes
#define LASTZARUNVERSION "181"

// Protocol version used in demos.
// Bump it if you change existing DEM_ commands or add new ones.
// Otherwise, it should be safe to leave it alone.
// [rc4l] 0x21B, NOT upstream's 0x21A. Upstream bumped 0x219->0x21A for the un-truncated map name in
// demos (uzdoom@4acc04ce6); we were ALREADY at 0x21A for an unrelated reason of our own (the
// SoundActor pitch field below), so reusing their number would mean two different demo formats
// sharing one version and every existing 0x21A demo being read with the wrong map-name width.
#define DEMOGAMEVERSION 0x21B	// [rc4l] 0x21A: SoundActor pitch field; 0x21B: full-length map names in demos

// Minimum demo version we can play.
// Bump it whenever you change or remove existing DEM_ commands.
#define MINDEMOVERSION 0x215

// SAVEVER is the version of the information stored in level snapshots.
// Note that SAVEVER is not directly comparable to VERSION.
// SAVESIG should match SAVEVER.

// MINSAVEVER is the minimum level snapshot version that can be loaded.
// [rc4l] Raised from 3100: widening fixed_t to 64-bit doubled the on-disk width of every
// serialized fixed_t field, so no snapshot at or below LAST_FIXED32_SAVEVER can be decoded.
#define MINSAVEVER	4507

// [rc4l] The last SAVEVER whose level snapshots serialized fixed_t as 32 bits. fixed_t is now
// 64-bit (basictypes.h), so FArchive writes 8 bytes per fixed_t field instead of 4. SAVEVER and
// MINSAVEVER MUST both exceed this whenever fixed_t is wider than 32 bits, or old saves load
// misaligned and corrupt actor state. A static_assert in p_saveg.cpp enforces this.
#define LAST_FIXED32_SAVEVER 4506

// Use 4500 as the base git save version, since it's higher than the
// SVN revision ever got.
// [MGOOOOOO] 4508: AActor now serializes projectilepassradius (guarded in AActor::Serialize).
// [rc4l] 4509: AActor now serializes the MBF21 damage-group fields (Infighting/Projectile/Splash
// group), guarded in AActor::Serialize.
// [ZandroX] 4510: APlayerPawn now serializes FullHeight (guarded in APlayerPawn::Serialize).
// [MGOOOOOO] 4511: AActor now serializes flags9 and the ripper controls + runtime budget state
// (guarded in AActor::Serialize; see features/ripper).
// [rc4l] 4512: level_info_t / FLevelLocals sky, fade, F1, border and background name fields became
// FStrings and the sky pair became FTextureIDs (uzdoom@65e8563cf), so the serialised layout changed
// and older saves must not be read back into it.

// [rc4l] 4513: map names in level snapshots are stored as full strings rather than a fixed
// 8-character field (uzdoom@8ec95dc58). Upstream numbered the same change 4508; ours is a separate
// line that was already past that.
// [rc4l] 4514: AActor now serializes FriendPlayer (guarded in AActor::Serialize), so a saved
// friendly actor keeps the player it belongs to (uzdoom@e1130b860). Upstream numbered the same
// change 4509; ours is a separate line that was already past that.
// [rc4l] 4515: dmflags bit 19 changed meaning -- it was DF_RESPAWN_SUPER, it is now DF_YES_FREELOOK,
// and respawn-super moved to dmflags2 bit 27 (uzdoom@a21f01bc5). G_DoLoadGame migrates older saves.
// [rc4l] 4516: AActor now serializes weaponspecial, the weapon scratch counter split out of
// special1 (uzdoom@ee6e87d94). Upstream bumped for the same change; ours is a separate line.
// [rc4l] 4517: DACSThinker serializes its script list iteratively, longest-last, with an
// explicit count, instead of letting the archive chase the linked list recursively
// (uzdoom@e3640b5bf + 5170abfee). Upstream numbered the same change 4515; ours is a separate
// line that was already past that.
// [rc4l] 4518: each ACS module's data size is stored alongside its name, so a save made
// against a different build of the same-named BEHAVIOR is refused rather than loaded as
// garbage (uzdoom@3437f4fca + c494063eb). Upstream numbered it 4516; ours is a separate line.
// [rc4l] 4519: AActor serializes DamageMultiply, the outgoing-damage scale reachable from
// DECORATE and from ACS via APROP_DamageMultiplier (uzdoom@99b2cfa14 + e303833e5).
// [rc4l] 4520: AActor serializes TeleFogSourceType/TeleFogDestType, the per-actor teleport fog
// classes reachable from DECORATE and from ACS SetTeleFog/SwapTeleFog (uzdoom@30acb7200 cluster).
// [rc4l] 4521: AActor serializes mvFlags and APlayerPawn serializes MvType, the Quake movement
// model and its flag word (features/quake-movement). Numbered above upstream's 4520 rather than
// reusing the 4513 this branch was written against: saves made by builds 4514-4520 do not contain
// these fields, and a >= 4513 guard would have read them out of a file that never had them.
#define SAVEVER 4522	// [rc4l] uzdoom quake cluster: per-axis intensities, flags, CountdownStart


#define SAVEVERSTRINGIFY2(x) #x
#define SAVEVERSTRINGIFY(x) SAVEVERSTRINGIFY2(x)
#define SAVESIG "ZDOOMSAVE" SAVEVERSTRINGIFY(SAVEVER)

#define DYNLIGHT

// This is so that derivates can use the same savegame versions without worrying about engine compatibility
#define GAMESIG "ZANDRONUM"
// [rc4l] The engine data pk3, named with this build's release key by src/zandronum/CMakeLists.txt so
// a stale one is not found rather than silently loaded. The fallback only exists for builds that
// bypass our CMake entirely.
#ifndef FUA_CORE_PK3_NAME
#define FUA_CORE_PK3_NAME "fua_core_dev.pk3"
#endif
#define BASEWAD FUA_CORE_PK3_NAME

// [rc4l] THE product display name. Every user-facing place that names this engine -- the console
// version line, the window/taskbar title -- reads this one macro, so a rebrand is a one-line edit
// here rather than a hunt through the tree.
//
// Deliberately NOT reusing GAMESIG / GAMENAME / GAMENAMELOWERCASE for this. Those look like display
// names but are load-bearing compatibility values: GAMENAMELOWERCASE is the config FILENAME
// (m_specialpaths.cpp) so changing it silently orphans everyone's settings, and GAMESIG identifies
// the engine inside savegames via GetEngineString() (g_game.cpp) so changing it invalidates saves.
// Renaming the product must not do either of those things.
#define FUA_NAME "Fua"

// More stuff that needs to be different for derivatives.
#define GAMENAME "Zandronum"
#define GAMENAMELOWERCASE "zandronum"
#define DOMAIN_NAME "zandronum.com"
#define FORUM_URL "https://" DOMAIN_NAME "/forum/"
#define BUGS_FORUM_URL	"https://" DOMAIN_NAME "/tracker/"
#define WIKI_URL "https://wiki." DOMAIN_NAME "/"

// [rc4l] Where a player is sent when their BUILD does not match a server's.
//
// Deliberately not DOMAIN_NAME. A protocol mismatch against a ZandroX server is a ZandroX build
// mismatch, and zandronum.com has nothing to offer that player -- it does not publish the build they
// are being told to go and find. The other DOMAIN_NAME uses are Zandronum's own services (the
// buy-Doom and Freedoom redirects) and stay where they are.
#define FUA_RELEASES_URL "https://github.com/rc4l/ForkUnderA/releases"

// [BC] This is what's displayed as the title for server windows.
#define	SERVERCONSOLE_TITLESTRING	GAMENAME " v" DOTVERSIONSTR " Server"

#if defined(__APPLE__) || defined(_WIN32)
#define GAME_DIR GAMENAME
#else
#define GAME_DIR ".config/" GAMENAMELOWERCASE
#endif


// The maximum length of one save game description for the menus.
#define SAVESTRINGSIZE		24

#endif //__VERSION_H__
