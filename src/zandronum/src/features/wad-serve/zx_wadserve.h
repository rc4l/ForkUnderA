// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Serving the server's own WADs to the players joining it, over HTTP.
//
// features/wad-download already fetches missing files from public mirrors, which is the right default
// and covers most joins. It cannot cover the case this exists for: a WAD that is on no mirror at all,
// because it was built ten minutes ago. Testing a map used to mean uploading it somewhere between
// every iteration, and that is the whole motivation -- the server already has the file open.
//
// WHY THIS IS A TCP LISTENER AND NOT THE GAME SOCKET
//
// Quake 3 and Source both send file bytes in-band over the game connection, and both later grew an
// HTTP escape hatch (sv_dlURL, sv_downloadurl) that became the thing everyone actually uses. Odamex
// skipped the middle step and serves no bytes at all. The in-band route means writing our own flow
// control, ACK and retransmit, on a socket that is also carrying ticcmds, paced by a 35 Hz loop.
// Over TCP the kernel does congestion control for us and we add a policy cap on top -- which is the
// entire difference between "implement a transport" and "implement a rate limit".
//
// Two consequences make it lopsided rather than merely nicer:
//
//   - THE CLIENT NEEDS ALMOST NO NEW CODE. It already fetches WADs over HTTP from a list of mirrors,
//     verifies hashes and applies the IWAD gate. The server's endpoint is one more entry in that
//     list. `curl http://host:10666/dwango5.wad` is a valid end-to-end test with no game involved.
//   - NO AMPLIFICATION. A spoofed-source UDP request could have made us fire a 200 MB WAD at a
//     victim who never asked. The TCP handshake proves the peer's address before a byte moves.
//
// The cost is real and worth stating: operators must forward TCP as well as UDP. Same port NUMBER as
// the game -- UDP 10666 and TCP 10666 are distinct bindings to the OS, so nothing about the existing
// netcode changes -- but a host who only opened UDP will see downloads fail while the server itself
// works fine. The status command exists largely to make that diagnosable.
//
// WHAT KEEPS IT FROM RUINING THE GAME IT IS ATTACHED TO
//
// The transfer threads cannot stall the main loop; they are separate threads on separate sockets. The
// contended resource is the uplink, and an uncapped transfer would fill it and leave tic packets
// queueing behind file bytes, which players experience as lag. So a global token bucket bounds every
// transfer together (see computation/ratebucket_compute.h for why a per-connection cap alone is a
// trap), slots bound how many run at once, and a per-address cap stops one peer taking them all.
//
// THREADING is the features/updater rule, unchanged: workers touch nothing the engine considers
// single-threaded. No Printf, no CVARs, no FString, no wad tables. The servable file table and the
// configuration are snapshotted on the main thread into plain types under a mutex; anything a worker
// wants to say goes into a queue that Tick() drains. Printf off the main thread has crashed this
// engine before.

#ifndef ZX_WADSERVE_H
#define ZX_WADSERVE_H

#include "zstring.h"

namespace zx { namespace wadserve {

// Whether the listener is bound and accepting right now.
bool IsActive();

// The TCP port being served on, or 0 when inactive. This is what the launcher protocol advertises.
int Port();

// Whether the operator would rather clients used public mirrors before asking this server. Advertised
// alongside the port so the client can order its list accordingly.
bool PrefersMirrors();

// Call once per frame from the main loop. Drains the worker log, notices configuration changes, and
// rebuilds the servable file table when the loaded WAD set changes. A no-op on a client.
void Tick();

// Stop accepting and close the listener. Transfers already in flight are cut off.
void Shutdown();

// A one-line summary for the console: port, active transfers, bytes served.
FString StatusLine();

}} // namespace zx::wadserve

#endif // ZX_WADSERVE_H
