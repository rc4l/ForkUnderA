# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Builds the dmflag name table straight out of the engine's own doomdef.h.
#
# Generated rather than transcribed because a wrong entry here does not break -- it lies. A server
# running instagib would be listed as running something else, and nothing about the output would look
# suspicious. There are 163 of these across six words; hand-copying them is a coin flip repeated 163
# times, and it goes stale the moment upstream adds one.
#
# Usage:
#   cmake -DDOOMDEF=<path to doomdef.h> -DOUT=<path> -P tools/gen-dmflagnames.cmake

if(NOT EXISTS "${DOOMDEF}")
	message(FATAL_ERROR "gen-dmflagnames: cannot read ${DOOMDEF}")
endif()

# Longest prefix first: DF2_ must be matched before DF_, and ZACOMPATF_/COMPATF2_ before COMPATF_.
set(PREFIXES ZACOMPATF_ COMPATF2_ COMPATF_ ZADF_ DF2_ DF_)
set(WORD_ZACOMPATF_ 4)
set(WORD_COMPATF2_ 5)
set(WORD_COMPATF_ 3)
set(WORD_ZADF_ 2)
set(WORD_DF2_ 1)
set(WORD_DF_ 0)

file(STRINGS "${DOOMDEF}" lines)
set(ENTRIES "")
set(COUNT 0)

foreach(line IN LISTS lines)
	# "NAME = M << S," with an optional trailing comment. Anything else in the header is not a flag.
	if(NOT line MATCHES "^[ \t]*([A-Z0-9_]+)[ \t]*=[ \t]*([0-9]+)[ \t]*<<[ \t]*([0-9]+)[ \t]*,")
		continue()
	endif()
	set(name "${CMAKE_MATCH_1}")
	set(mult "${CMAKE_MATCH_2}")
	set(shift "${CMAKE_MATCH_3}")

	set(word "")
	set(label "")
	foreach(p IN LISTS PREFIXES)
		string(LENGTH "${p}" plen)
		string(SUBSTRING "${name}" 0 ${plen} head)
		if(head STREQUAL "${p}")
			set(word "${WORD_${p}}")
			string(SUBSTRING "${name}" ${plen} -1 label)
			break()
		endif()
	endforeach()
	if(word STREQUAL "")
		continue()
	endif()

	math(EXPR value "${mult} << ${shift}")
	if(value EQUAL 0)
		continue()
	endif()

	# Underscores to spaces; the Doom small font is uppercase anyway, so no case juggling.
	string(REPLACE "_" " " label "${label}")

	string(APPEND ENTRIES "\t{ ${word}, ${value}, \"${label}\" },\n")
	math(EXPR COUNT "${COUNT} + 1")
endforeach()

if(COUNT EQUAL 0)
	message(FATAL_ERROR "gen-dmflagnames: parsed ${DOOMDEF} and found no flags -- the enum shape must have changed")
endif()

file(WRITE "${OUT}"
"// GENERATED FILE -- do not edit.
//
// Built by tools/gen-dmflagnames.cmake from src/zandronum/src/doomdef.h -- ${COUNT} flags across the
// six words a server sends with SQF_ALL_DMFLAGS, in this order:
//   0 dmflags   1 dmflags2   2 zadmflags   3 compatflags   4 zacompatflags   5 compatflags2
//
// `value` is NOT always a single bit: dmflags encodes the falling-damage style as a two-bit field
// (1<<3, 2<<3, 3<<3), so testing these with a plain AND reports the wrong one. See
// ComputeSetFlagNames, which is what the drawing goes through.

#ifndef ZX_DMFLAGNAMES_H
#define ZX_DMFLAGNAMES_H

namespace zx {

struct DmFlagName
{
	int word;
	unsigned int value;
	const char *name;
};

const DmFlagName kDmFlagNames[] = {
${ENTRIES}};
const int kDmFlagNameCount = ${COUNT};

} // namespace zx

#endif // ZX_DMFLAGNAMES_H
")
message(STATUS "gen-dmflagnames: ${COUNT} flags")
