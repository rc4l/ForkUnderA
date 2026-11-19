//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: apply.h
//
// Description: The client's snapshot commands: the snapshot datagram
// and its ack, the door sound and the sound table. cl_main.cpp's dispatch
// calls these; each reads its command's body from the stream.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_APPLY_H
#define ZX_NETSNAPSHOT_APPLY_H

#include "basictypes.h"

struct BYTESTREAM_s;

namespace zx { namespace netsnapshot {

void OnSnapshot( BYTESTREAM_s *pByteStream );
void OnDoorSound( BYTESTREAM_s *pByteStream );
void OnFloorSound( BYTESTREAM_s *pByteStream );
void OnSoundTable( BYTESTREAM_s *pByteStream );

// Datagrams refused as too late to count (ComputeAckTooLate), since the last
// reset; for the frame order line.
unsigned int TooLate( bool reset );

// Datagrams refused for arriving past the ack history, apart from the too-late
// count, and the SnapJoins that reset the ack state; for the frame order line.
unsigned int PastHistory( bool reset );
unsigned int AckResets( bool reset );
// Times a run of SNAP_ACK_HISTORY refusals past the history, nothing accepted,
// started the ack state again, which only another stream's sequence causes.
unsigned int AckRecoveries( bool reset );

}} // namespace zx::netsnapshot

// cl_main.cpp's: a snapshot's newest reliable sequence, against its packet
// sequence state.
void CLIENT_NoteReliableSequence( WORD low );

#endif
