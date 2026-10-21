// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/bot-save/zx_botsave.h"

#include "features/bot-save/computation/botsave_compute.h"

#include "bots.h"
#include "d_player.h"
#include "doomstat.h"
#include "m_png.h"
#include "network.h"

#include <vector>

namespace zx
{

namespace
{

// [rc4l] The chunk's four-character name, in the same shape as the engine's own: mpEm for the
// skirmish flag, ptIc for the play time, snXt for the next skill.
const DWORD kBotChunk = MAKE_ID( 'b', 'o', 't', 'S' );

} // namespace

void BotSave_Write( FILE *file )
{
	if ( file == NULL )
		return;

	std::vector<BotSnapshot> bots;

	for ( ULONG i = 0; i < MAXPLAYERS; ++i )
	{
		if (( playeringame[i] == false ) || ( players[i].bIsBot == false ))
			continue;

		const CSkullBot *pBot = players[i].pSkullBot;
		if ( pBot == NULL )
			continue;

		BotSnapshot b;
		b.slot = static_cast<int>( i );
		b.name = players[i].userinfo.GetName( );
		b.team = "";

		// Tier one only; see the computation header for what is left out and why.
		b.forwardMove = pBot->m_lForwardMove;
		b.sideMove = pBot->m_lSideMove;
		b.forwardMovePersist = pBot->m_bForwardMovePersist;
		b.sideMovePersist = pBot->m_bSideMovePersist;
		b.buttons = pBot->m_lButtons;
		b.aimAtEnemy = pBot->m_bAimAtEnemy;
		b.aimAtEnemyDelay = pBot->m_ulAimAtEnemyDelay;
		b.angleDelta = pBot->m_AngleDelta;
		b.angleOffBy = pBot->m_AngleOffBy;
		b.angleDesired = pBot->m_AngleDesired;
		b.turnLeft = pBot->m_bTurnLeft;
		b.pathType = pBot->m_ulPathType;
		b.skillIncrease = pBot->m_bSkillIncrease;
		b.skillDecrease = pBot->m_bSkillDecrease;
		b.lastMedalReceived = pBot->m_lLastMedalReceived;

		bots.push_back( b );
	}

	const std::vector<unsigned char> bytes = SerialiseBots( bots );
	if ( bytes.empty( ))
		return;			// no chunk at all, which is what every save before this looked like

	M_AppendPNGChunk( file, kBotChunk, &bytes[0], static_cast<DWORD>( bytes.size( )));
}

void BotSave_Restore( PNGHandle *png, FILE *file )
{
	if (( png == NULL ) || ( file == NULL ))
		return;

	const unsigned int len = M_FindPNGChunk( png, kBotChunk );
	if ( len == 0 )
		return;			// no bots in this save, or a save written before this existed

	std::vector<unsigned char> bytes( len );
	if ( fread( &bytes[0], 1, len, file ) != len )
		return;

	std::vector<BotSnapshot> bots;
	if ( ParseBots( &bytes[0], bytes.size( ), bots ) == false )
		return;			// a chunk we cannot read is "no bots", never a reason to fail the load

	for ( size_t i = 0; i < bots.size( ); ++i )
	{
		const BotSnapshot &b = bots[i];

		if (( b.slot < 0 ) || ( b.slot >= MAXPLAYERS ))
			continue;

		// [rc4l] Never re-occupy a slot somebody is already in. The human is in one of these, and a
		// save carrying a stale slot number must not be able to evict them.
		if ( playeringame[b.slot] )
			continue;

		// [rc4l] An unknown name is handed over as NULL rather than as itself.
		//
		// The constructor looks a name up in the bot definitions and, finding nothing, leaves its
		// index at g_BotInfo.Size() and then indexes with it. Passing NULL takes the branch that
		// picks a random revealed bot instead. That happens whenever a save is loaded without the
		// mod whose bots it used, so it is a normal case rather than a corrupt one.
		const bool bKnown = ( b.name.empty( ) == false ) && BOTS_IsValidName( b.name.c_str( ));

		// The constructor claims the slot, sets bIsBot and pSkullBot, and fills in the userinfo. It
		// is the same path addbot takes.
		CSkullBot *pBot = new CSkullBot( bKnown ? b.name.c_str( ) : NULL,
			b.team.empty( ) ? NULL : b.team.c_str( ), static_cast<ULONG>( b.slot ));

		pBot->m_lForwardMove = b.forwardMove;
		pBot->m_lSideMove = b.sideMove;
		pBot->m_bForwardMovePersist = b.forwardMovePersist;
		pBot->m_bSideMovePersist = b.sideMovePersist;
		pBot->m_lButtons = b.buttons;
		pBot->m_bAimAtEnemy = b.aimAtEnemy;
		pBot->m_ulAimAtEnemyDelay = b.aimAtEnemyDelay;
		pBot->m_AngleDelta = b.angleDelta;
		pBot->m_AngleOffBy = b.angleOffBy;
		pBot->m_AngleDesired = b.angleDesired;
		pBot->m_bTurnLeft = b.turnLeft;
		pBot->m_ulPathType = b.pathType;
		pBot->m_bSkillIncrease = b.skillIncrease;
		pBot->m_bSkillDecrease = b.skillDecrease;
		pBot->m_lLastMedalReceived = b.lastMedalReceived;
	}
}

} // namespace zx
