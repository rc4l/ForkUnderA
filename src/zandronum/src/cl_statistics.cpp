//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2003-2007 Brad Carney
// Copyright (C) 2007-2012 Skulltag Development Team
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the Skulltag Development Team nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 4. Redistributions in any form must be accompanied by information on how to
//    obtain complete source code for the software and any accompanying
//    software that uses the software. The source code must either be included
//    in the distribution or be available for no more than the cost of
//    distribution plus a nominal fee, and must be freely redistributable
//    under reasonable conditions. For an executable file, complete source
//    code means the source code for all modules it contains. It does not
//    include source code for modules or files that typically accompany the
//    major components of the operating system on which the executable file
//    runs.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Date created:  8/19/07
//
//
// Filename: cl_statistics.cpp
//
// Description: Keeps track of the amount of data sent to and from the server,
// and possibly other related things.
//
//-----------------------------------------------------------------------------

#include "cl_statistics.h"
#include "doomtype.h"
#include "stats.h"
#include "doomdef.h"
#include "doomstat.h"
#include "features/net-client/computation/netstats_compute.h"   // [KI] the nettraffic percentage
#include "features/net-client/computation/netgraph_compute.h"   // [rc4l] cl_netgraph's layout
#include "c_cvars.h"
#include "networkshared.h"
#include "v_font.h"
#include "v_palette.h"
#include "v_video.h"
#include <vector>

//*****************************************************************************
//	CLASSES

// [BB]
class StatTracker
{
	unsigned int _value;
	unsigned int _valueThisTick;
	unsigned int _valueThisSecond;
	unsigned int _valueLastSecond;
	unsigned int _maxValuePerSecond;

	// [AK] Values for compressed stats.
	// [KI] Ported from zandronum@173f74aa07b893e0c6451f6947ae20f45eb733c2 and zandronum@1a03a82d780ac7923770605aa631865c04076240: the wire-size and zstd-count lanes beside each byte count.
	unsigned int _compressedValueThisTick;
	unsigned int _compressedValueThisSecond;
	unsigned int _compressedValueLastSecond;

	unsigned int _packetsThisTick;
	unsigned int _packetsThisSecond;
	unsigned int _packetsLastSecond;

	unsigned int _numZstdThisTick;
	unsigned int _numZstdThisSecond;
	unsigned int _numZstdLastSecond;

	// [rc4l] Ported from zandronum@9a8ae9d825e8c2a72bcf0cd644c59da13d55fd70: each second's value over the last minute, for the net graph.
	unsigned int _maxValueLastMinute;
	RingBuffer<unsigned int, zx::netclient::NETGRAPH_SECONDS> _valuesLastMinute;

public:
	StatTracker ( )
	{
		Clear ( );
	}

	void Clear ( )
	{
		_value = 0;
		_valueThisTick = 0;
		_valueThisSecond = 0;
		_valueLastSecond = 0;
		_maxValuePerSecond = 0;
		_compressedValueThisTick = 0;
		_compressedValueThisSecond = 0;
		_compressedValueLastSecond = 0;
		_packetsThisTick = 0;
		_packetsThisSecond = 0;
		_packetsLastSecond = 0;
		_numZstdThisTick = 0;
		_numZstdThisSecond = 0;
		_numZstdLastSecond = 0;
		_maxValueLastMinute = 0;
		_valuesLastMinute.clear( );
	}

	void AddToTic ( const int Value, const int CompressedValue = 0, const bool WasZstd = false )
	{
		_valueThisTick += Value;

		// [AK] Add to the compressed value for this tick.
		_compressedValueThisTick += CompressedValue;

		_packetsThisTick++;
		if (WasZstd)
			_numZstdThisTick++;
	}

	void TicPassed ( )
	{
		_value += _valueThisTick;
		_valueThisSecond += _valueThisTick;
		_valueThisTick = 0;

		// [AK] Add the compressed value from this tick to this second too.
		_compressedValueThisSecond += _compressedValueThisTick;
		_compressedValueThisTick = 0;

		_packetsThisSecond += _packetsThisTick;
		_packetsThisTick = 0;

		_numZstdThisSecond += _numZstdThisTick;
		_numZstdThisTick = 0;
	}

	void SecondPassed ( )
	{
		_valueLastSecond = _valueThisSecond;
		_valueThisSecond = 0;
		if ( _maxValuePerSecond < _valueLastSecond )
			_maxValuePerSecond = _valueLastSecond;

		// [AK] Save the compressed value from this second too.
		_compressedValueLastSecond = _compressedValueThisSecond;
		_compressedValueThisSecond = 0;

		_packetsLastSecond = _packetsThisSecond;
		_packetsThisSecond = 0;

		_numZstdLastSecond = _numZstdThisSecond;
		_numZstdThisSecond = 0;

		// [AK] Add the uncompressed value from this last second to the buffer of
		// values in the last minute, then determine the new maximum value.
		_valuesLastMinute.put( _valueLastSecond );
		unsigned int lastMinute[zx::netclient::NETGRAPH_SECONDS];
		for ( unsigned int i = 0; i < zx::netclient::NETGRAPH_SECONDS; i++ )
			lastMinute[i] = _valuesLastMinute.getOldestEntry( i );
		_maxValueLastMinute = zx::netclient::ComputeLargestValue( lastMinute, zx::netclient::NETGRAPH_SECONDS );
	}

	unsigned int getTotalValue ( ) const
	{
		return _value;
	}

	unsigned int getValueThisTick ( ) const
	{
		return _valueThisTick;
	}

	unsigned int getValueLastSecond ( ) const
	{
		return _valueLastSecond;
	}

	unsigned int getMaxValuePerSecond ( ) const
	{
		return _maxValuePerSecond;
	}

	// [AK] Returns a percentage of compressed to uncompressed stats.
	float getCompressionFactorLastSecond ( ) const
	{
		return zx::netclient::ComputeSharePercent( _compressedValueLastSecond, _valueLastSecond );
	}

	float getZstdPercentageLastSecond ( ) const
	{
		return zx::netclient::ComputeSharePercent( _numZstdLastSecond, _packetsLastSecond );
	}

	// [AK] Returns the largest uncompressed value in the last minute.
	unsigned int getMaxValueLastMinute ( ) const
	{
		return _maxValueLastMinute;
	}

	// [AK] Returns any uncompressed value in the last minute, from 0-59 seconds.
	unsigned int getValueLastMinute ( unsigned int second ) const
	{
		return ( second < zx::netclient::NETGRAPH_SECONDS ? _valuesLastMinute.getOldestEntry( second ) : 0 );
	}
};

//*****************************************************************************
// [rc4l] Ported from zandronum@9a8ae9d825e8c2a72bcf0cd644c59da13d55fd70: a graph of its lines' last minute, its layout from netgraph_compute.
class NetGraph
{
public:
	NetGraph( const char *label, const char *scaleLabel, unsigned int scaleDivider, unsigned int defaultScaleMax, unsigned int scaleBonus ) :
		label( label ),
		scaleLabel( scaleLabel ),
		scaleDivider( scaleDivider > 0 ? scaleDivider : 1 ),
		defaultScaleMax( defaultScaleMax ),
		scaleBonus( scaleBonus ) { }

	void ClearLines( )
	{
		lines.Clear( );
	}

	void AddLine( const StatTracker &tracker, const char *name, PalEntry color )
	{
		Line line = { &tracker, name, color };
		lines.Push( line );
	}

	void Draw( const int leftX, const int topY ) const
	{
		using namespace zx::netclient;
		const int rightX = leftX + NETGRAPH_WIDTH;
		const int bottomY = topY + NETGRAPH_HEIGHT;
		const uint32 gridColor = MAKERGB( 48, 48, 48 );
		const uint32 borderColor = MAKERGB( 64, 64, 64 );
		int textYOffset = 0;

		std::vector<unsigned int> lineMaxes;
		for ( unsigned int i = 0; i < lines.Size( ); i++ )
			lineMaxes.push_back( lines[i].tracker->getMaxValueLastMinute( ));
		const unsigned int scaleMax = ComputeNetGraphScaleMax( defaultScaleMax, scaleBonus, lineMaxes );

		// [AK] Draw the background of the net graph first.
		screen->Dim( MAKERGB( 16, 16, 16 ), 0.65f, leftX, topY, NETGRAPH_WIDTH, NETGRAPH_HEIGHT );

		// [AK] Next, draw the gridlines. The vertical lines move left as time goes.
		for ( int x : ComputeNetGraphVerticalGridLines( leftX, gametic / TICRATE ))
			screen->DrawLine( x, topY, x, bottomY, -1, gridColor );
		for ( int y : ComputeNetGraphHorizontalGridLines( topY ))
			screen->DrawLine( leftX, y, rightX, y, -1, gridColor );

		// [AK] Draw each line to be plotted on the graph.
		for ( unsigned int i = 0; i < lines.Size( ); i++ )
		{
			for ( unsigned int second = 1; second < NETGRAPH_SECONDS; second++ )
			{
				screen->DrawLine( ComputeNetGraphPointX( leftX, second - 1 ), ComputeNetGraphPointY( lines[i].tracker->getValueLastMinute( second - 1 ), bottomY, scaleMax ),
					ComputeNetGraphPointX( leftX, second ), ComputeNetGraphPointY( lines[i].tracker->getValueLastMinute( second ), bottomY, scaleMax ), -1, lines[i].color );
			}
		}

		// [AK] If this net graph has more than one line to draw, add a legend.
		if ( lines.Size( ) > 1 )
		{
			std::vector<NetGraphLegendEntry> entries;
			for ( unsigned int i = 0; i < lines.Size( ); i++ )
				entries.push_back( { lines[i].name.IsNotEmpty( ), ConFont->StringWidth( lines[i].name.GetChars( )) } );

			const std::vector<NetGraphLegendPlace> places = ComputeNetGraphLegend( leftX, topY, entries, ConFont->GetHeight( ));
			for ( unsigned int i = 0; i < lines.Size( ); i++ )
			{
				if ( places[i].drawn == false )
					continue;

				screen->DrawLine( places[i].x, places[i].y, places[i].x + NETGRAPH_LEGEND_LINE_WIDTH, places[i].y, -1, lines[i].color );
				const int textY = places[i].y - ConFont->StringHeight( lines[i].name.GetChars( ), &textYOffset ) / 2;
				screen->DrawText( ConFont, CR_GREY, places[i].x + NETGRAPH_LEGEND_LINE_WIDTH + NETGRAPH_LEGEND_SPACING, textY - textYOffset, lines[i].name.GetChars( ), TAG_DONE );
			}
		}

		// [AK] Draw the borders around the graph after drawing the lines.
		screen->DrawLine( leftX, topY, rightX, topY, -1, borderColor );
		screen->DrawLine( leftX, bottomY, rightX, bottomY, -1, borderColor );
		screen->DrawLine( leftX, topY, leftX, bottomY, -1, borderColor );
		screen->DrawLine( rightX, topY, rightX, bottomY, -1, borderColor );

		// [AK] Draw the graph's label in the top-center, if applicable.
		if ( label.IsNotEmpty( ))
		{
			const int textX = leftX + ( NETGRAPH_WIDTH - ConFont->StringWidth( label.GetChars( ))) / 2;
			const int textY = topY - ConFont->StringHeight( label.GetChars( ), &textYOffset ) - 2;
			screen->DrawText( ConFont, CR_WHITE, textX, textY - textYOffset, label.GetChars( ), TAG_DONE );
		}

		// [AK] Draw the min and max scale values on the right, and the label if there is one.
		FString scaleText = "0";
		const int textY = bottomY - ConFont->StringHeight( scaleText.GetChars( ), &textYOffset );
		screen->DrawText( ConFont, CR_WHITE, rightX + 2, textY - textYOffset, scaleText.GetChars( ), TAG_DONE );

		scaleText.Format( "%u", ComputeNetGraphScaleTop( scaleMax, scaleDivider ));
		if ( scaleLabel.IsNotEmpty( ))
			scaleText.AppendFormat( " %s", scaleLabel.GetChars( ));
		screen->DrawText( ConFont, CR_WHITE, rightX + 2, topY, scaleText.GetChars( ), TAG_DONE );
	}

private:
	struct Line
	{
		const StatTracker *tracker;
		FString name;
		PalEntry color;
	};

	TArray<Line> lines;
	const FString label;
	const FString scaleLabel;
	const unsigned int scaleDivider;
	const unsigned int defaultScaleMax;
	const unsigned int scaleBonus;
};

//*****************************************************************************
//	VARIABLES

static	StatTracker			g_bytesSentStatTracker;
static	StatTracker			g_bytesReceivedStatTracker;
static	StatTracker			g_missingPacketsRequestedStatTracker;

static	NetGraph			g_NetTrafficGraph( "Network Traffic", "KB/s", 1000, 6000, 2000 );
static	NetGraph			g_MissingPacketsGraph( "Missing Packets", nullptr, 1, 10, 2 );

//*****************************************************************************
//	CONSOLE VARIABLES

// [AK] If enabled, shows the net graph when the "nettraffic" stat is active.
CVAR( Bool, cl_netgraph, false, CVAR_ARCHIVE | CVAR_NOSETBYACS )

//*****************************************************************************
//	FUNCTIONS

void CLIENTSTATISTICS_Construct( void )
{
	g_bytesSentStatTracker.Clear();
	g_bytesReceivedStatTracker.Clear();
	g_missingPacketsRequestedStatTracker.Clear();

	// [AK] Add each of the lines to their respective net graphs.
	g_NetTrafficGraph.ClearLines( );
	g_MissingPacketsGraph.ClearLines( );
	g_NetTrafficGraph.AddLine( g_bytesReceivedStatTracker, "In", MAKERGB( 255, 20, 35 ));
	g_NetTrafficGraph.AddLine( g_bytesSentStatTracker, "Out", MAKERGB( 100, 255, 55 ));
	g_MissingPacketsGraph.AddLine( g_missingPacketsRequestedStatTracker, "Missing", MAKERGB( 255, 20, 35 ));
}

//*****************************************************************************
//
void CLIENTSTATISTICS_Tick( void )
{
	// Add to the number of bytes sent/received this second.
	g_bytesSentStatTracker.TicPassed();
	g_bytesReceivedStatTracker.TicPassed();
	g_missingPacketsRequestedStatTracker.TicPassed();

	// Every second, update the number of bytes sent last second with the number of bytes
	// sent this second, and then reset the number of bytes sent this second.
	if (( gametic % TICRATE ) == 0 )
	{
		g_bytesSentStatTracker.SecondPassed ( );
		g_bytesReceivedStatTracker.SecondPassed ( );
		g_missingPacketsRequestedStatTracker.SecondPassed ( );
	}
}

//*****************************************************************************
//
void CLIENTSTATISTICS_AddToBytesSent( unsigned int uncompressedBytes, unsigned int compressedBytes, bool wasZstdPacket )
{
	g_bytesSentStatTracker.AddToTic ( uncompressedBytes, compressedBytes, wasZstdPacket );
}

//*****************************************************************************
//
void CLIENTSTATISTICS_AddToBytesReceived( unsigned int uncompressedBytes, unsigned int compressedBytes, bool wasZstdPacket )
{
	g_bytesReceivedStatTracker.AddToTic ( uncompressedBytes, compressedBytes, wasZstdPacket );
}

//*****************************************************************************
//
void CLIENTSTATISTICS_AddToMissingPacketsRequested( unsigned int Num )
{
	g_missingPacketsRequestedStatTracker.AddToTic ( Num );
}

//*****************************************************************************
//
void CLIENTSTATISTICS_DrawNetGraph( int xPos, int yPos )
{
	if ( cl_netgraph == false )
		return;

	g_NetTrafficGraph.Draw( xPos, zx::netclient::ComputeNetGraphTopY( yPos, 0, ConFont->GetHeight( )));
	g_MissingPacketsGraph.Draw( xPos, zx::netclient::ComputeNetGraphTopY( yPos, 1, ConFont->GetHeight( )));
}

//*****************************************************************************
//	STATISTICS

ADD_STAT( nettraffic )
{
	FString out;

	// [KI] Ported from zandronum@1a03a82d780ac7923770605aa631865c04076240 at zandronum@2a65e4bcf494a3bde63a18f006ece9154d604e19's one-line-per-statistic layout.
	out.Format( "In:   %5u/%5u/%5u (comp: %.1f%%, zstd: %.1f%%)\nOut:  %5u/%5u/%5u (comp: %.1f%%, zstd: %.1f%%)\nLoss: %5u/%5u",
		g_bytesReceivedStatTracker.getValueThisTick( ),
		g_bytesReceivedStatTracker.getValueLastSecond( ),
		g_bytesReceivedStatTracker.getMaxValuePerSecond( ),
		g_bytesReceivedStatTracker.getCompressionFactorLastSecond( ),
		g_bytesReceivedStatTracker.getZstdPercentageLastSecond(),
		g_bytesSentStatTracker.getValueThisTick( ),
		g_bytesSentStatTracker.getValueLastSecond( ),
		g_bytesSentStatTracker.getMaxValuePerSecond( ),
		g_bytesSentStatTracker.getCompressionFactorLastSecond( ),
		g_bytesSentStatTracker.getZstdPercentageLastSecond(),
		g_missingPacketsRequestedStatTracker.getValueLastSecond( ),
		g_missingPacketsRequestedStatTracker.getMaxValuePerSecond( ));

	return out;
}
