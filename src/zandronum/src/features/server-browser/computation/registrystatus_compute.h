// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What happened when we asked a server registry for its list, and how to say it.
//
// EVERY STATUS HERE IS ONE WE HAVE ACTUALLY SEEN. That is the rule, and it is worth writing down,
// because a status enum invented at a desk grows codes nobody ever hits and then nobody trusts any of
// them. Three of these are refusals the protocol already sends us and that we already parse in
// cl_main; the rest are the ways the conversation fails before a reply can exist.
//
// The one that started this: a browser that queried the right registry and then discarded its reply
// showed exactly the same blank list as a browser that could not reach anything at all. There was no
// way, from the screen, to tell those apart. This is that missing distinction.
//
// Header-pure by the features/ rules: no engine types, so the menu maps a tone onto its own colours.

#ifndef ZX_REGISTRYSTATUS_COMPUTE_H
#define ZX_REGISTRYSTATUS_COMPUTE_H

#include <string>

namespace zx
{

enum class RegistryStatus
{
	// Asked, nothing back yet. Not a verdict.
	Pending,

	// It answered with a list. The only good outcome.
	Ok,

	// [rc4l] DNS had no address for the name, so NOTHING WAS EVER SENT. A typo in the list, or DNS
	// being down. Distinct from NoAnswer because the fix is different: this one is wrong in the
	// setting, not wrong on the network.
	LookupFailed,

	// We had an address, packets went out, and nothing came back before we gave up. The host is down,
	// the port is shut, or nothing there is a registry. A literal IP always looks up fine, so a dead
	// local registry lands here rather than in LookupFailed.
	NoAnswer,

	// SRSC_REQUESTIGNORED: we asked again too soon. Clears by itself in a few seconds, which is why it
	// is not coloured like the failures that do not.
	Throttled,

	// SRSC_IPISBANNED.
	Banned,

	// SRSC_WRONGVERSION: it speaks a different launcher protocol.
	Version,
};

// [rc4l] Four tones, not seven colours. The player needs to know whether to do something, and only
// three answers matter: nothing yet, fine, and broken, plus "broken but it will fix itself", which
// earns its own tone because telling someone to go and configure their router over a three second
// throttle is the same mistake ProbeDisplayFor exists to avoid.
enum class RegistryTone
{
	Waiting,
	Good,
	Warn,		// it refused, but it will stop refusing on its own
	Bad,
};

RegistryTone RegistryToneFor(RegistryStatus status);

// Whether the exchange is over. Pending is the only one that is not.
bool RegistryStatusIsFinished(RegistryStatus status);

// The stable short code, for the tooltip and for anything that ends up in a log.
const char *RegistryStatusCode(RegistryStatus status);

// The same thing in words, because a code alone tells you nothing you did not already know from the
// colour.
const char *RegistryStatusText(RegistryStatus status);

// [rc4l] The hover text, three lines: which registry, the code, then what it means. The address goes
// first because with several configured, "which one is this" is the question the pointer is asking,
// and the code sits on its own line because it is the part someone quotes back at us.
//
// A port of zero is left off rather than printed, which is what a failed lookup has.
std::string RegistryTooltip(const std::string &host, int port, RegistryStatus status);

// [rc4l] What a recorded status should decay to once it is old enough to stop meaning anything.
//
// Throttled is the one that needs this, and it went unnoticed because the header above says it
// "clears by itself in a few seconds" -- it did not. Nothing ever cleared it: the expiry pass only
// turned Pending into NoAnswer, so one REQUESTIGNORED left the bar orange until some later reply
// happened to overwrite it, long after the registry had stopped ignoring us. Reported as a status
// bar stuck on REG_THROTTLED while the browser was working fine.
//
// It decays to Pending rather than Ok, because "they were busy a moment ago" is not evidence that
// they are answering now. Pending is the honest thing to say until we have asked again.
//
// Every other status is a fact about a conversation that finished -- banned, wrong version, no
// answer at all -- and stays until a new answer replaces it. Nothing about waiting makes those
// less true.
RegistryStatus AgeRegistryStatus(RegistryStatus current, int msSinceRecorded, int throttleClearMs);

} // namespace zx

#endif // ZX_REGISTRYSTATUS_COMPUTE_H
