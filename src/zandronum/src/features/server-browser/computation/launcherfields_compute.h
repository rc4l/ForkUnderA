// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Walking the SQF2 extended-info block of a launcher reply.
//
// This exists because of a bug that cost a long diagnosis and would have cost another one.
//
// A server's direct-download port read as 6400 instead of 10777, deterministically, and every
// download from it failed while curl fetched the same file from the same endpoint. The cause was one
// byte: SQF2_VOICECHAT is bit 4, immediately before the download field at bit 5, and it writes a
// single byte that the browser never consumed. Everything after it was read one byte early.
//
// The arithmetic is worth keeping, because it is what finally identified it. 10777 is 0x2A19, so the
// wire holds [flags 0x00][0x19][0x2A]. Reading one byte early takes sv_allowvoicechat's 1 as the
// flags byte -- hence a server reporting "prefers mirrors" that had never asked to -- and then reads
// [0x00][0x19] as the port: 0x1900 = 6400. A plausible-looking number, not obvious garbage, which is
// exactly why it survived so long.
//
// THE STRUCTURAL FIX, and the reason this is a compute unit rather than three more lines in
// browser.cpp: fields here are VARIABLE LENGTH, so an unrecognised bit cannot be skipped. A parser
// that meets one has already lost its place and everything it reads afterwards is fiction. So this
// refuses the whole block on an unknown bit rather than returning confident nonsense, and the caller
// keeps its previous values instead of adopting garbage.
//
// That turns "somebody adds an SQF2 bit and forgets the client" from a silent corruption that reads
// as a valid value into a loud, testable refusal. Adding a field means adding it in one place here,
// and the test that every flag combination consumes exactly what it was given catches the omission.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_LAUNCHERFIELDS_COMPUTE_H
#define ZX_LAUNCHERFIELDS_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

// The SQF2 bits, spelled out so this unit stays engine-free. Must match sv_main.h; the test asserts
// the set below is exactly what the parser handles, so a new bit cannot be added silently.
const unsigned kSqf2PwadHashes			= 0x00000001;
const unsigned kSqf2Country				= 0x00000002;
const unsigned kSqf2GameModeName		= 0x00000004;
const unsigned kSqf2GameModeShortName	= 0x00000008;
const unsigned kSqf2VoiceChat			= 0x00000010;
const unsigned kSqf2DirectDownload		= 0x00000020;
const unsigned kSqf2IwadHash			= 0x00000040;

enum class ExtendedParse
{
	Ok,
	Truncated,			// the block ended mid-field; nothing after it can be trusted
	UnknownField,		// a bit this build cannot consume -- refuse rather than lose our place
};

struct LauncherExtendedInfo
{
	std::vector<std::string> pwadHashes;
	std::string countryCode;			// exactly 3 characters, not null-terminated on the wire
	std::string gameModeName;
	std::string gameModeShortName;
	int voiceChat;
	int directDownloadPort;				// 0 when the server is not serving
	bool prefersMirrors;
	std::string iwadHash;

	LauncherExtendedInfo() : voiceChat(0), directDownloadPort(0), prefersMirrors(false) {}
};

// Every bit this build knows how to consume. Anything outside it makes ParseExtendedInfo refuse.
unsigned KnownExtendedFields();

// Walk the SQF2 block. Fields appear in ascending bit order, which is the order the server writes
// them (its dispatch table is keyed by bit value). `outConsumed` is how many bytes were read, which
// is what lets a test assert the parser consumes exactly what a writer produced.
ExtendedParse ParseExtendedInfo(const unsigned char *data, size_t length, unsigned flags2,
	LauncherExtendedInfo &out, size_t &outConsumed);

} // namespace zx

#endif // ZX_LAUNCHERFIELDS_COMPUTE_H
