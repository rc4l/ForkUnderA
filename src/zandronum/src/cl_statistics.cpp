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
};

//*****************************************************************************
//	VARIABLES

static	StatTracker			g_bytesSentStatTracker;
static	StatTracker			g_bytesReceivedStatTracker;
static	StatTracker			g_missingPacketsRequestedStatTracker;

//*****************************************************************************
//	FUNCTIONS

void CLIENTSTATISTICS_Construct( void )
{
	g_bytesSentStatTracker.Clear();
	g_bytesReceivedStatTracker.Clear();
	g_missingPacketsRequestedStatTracker.Clear();
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
