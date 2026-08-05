# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Compiles the two repo-root WAD-download lists into a C++ header, so iwadallowlist.txt and
# waddownloadsites.txt are the single source of truth and cannot drift from a hand-maintained array.
#
# Generated at BUILD time rather than checked in, and read from the binary rather than from disk at
# runtime, because the allowlist is a legal gate: it has to be something we shipped, not something a
# player or a mod can append "doom2.wad" to. A pull request is the only way to add a line, which is
# the right amount of friction for a claim about someone else's licence.
#
# Usage:
#   cmake -DIWAD_LIST=<path> -DSITE_LIST=<path> -DSUBST_LIST=<path> -DOUT=<path> \
#         -P tools/gen-wadlists.cmake

# Pull the first whitespace-separated token from each non-comment, non-blank line. Everything after
# it is documentation for the humans editing the file.
function(zx_read_tokens PATH OUT_VAR)
	set(tokens "")
	if(NOT EXISTS "${PATH}")
		message(FATAL_ERROR "gen-wadlists: missing input ${PATH}")
	endif()
	file(STRINGS "${PATH}" lines)
	foreach(line IN LISTS lines)
		string(STRIP "${line}" line)
		if(line STREQUAL "" OR line MATCHES "^#")
			continue()
		endif()
		# Split on the first run of whitespace; keep only what precedes it.
		string(REGEX REPLACE "[ \t].*$" "" token "${line}")
		if(NOT token STREQUAL "")
			list(APPEND tokens "${token}")
		endif()
	endforeach()
	set(${OUT_VAR} "${tokens}" PARENT_SCOPE)
endfunction()

# The substitutes file carries two meaningful tokens per line -- what a server asked for, and what we
# load instead -- collected as a flat "a;b;a;b" list so the emitted table stays a plain array of pairs.
function(zx_read_pairs PATH OUT_VAR)
	set(pairs "")
	if(NOT EXISTS "${PATH}")
		message(FATAL_ERROR "gen-wadlists: missing input ${PATH}")
	endif()
	file(STRINGS "${PATH}" lines)
	foreach(line IN LISTS lines)
		string(STRIP "${line}" line)
		if(line STREQUAL "" OR line MATCHES "^#")
			continue()
		endif()
		if(line MATCHES "^([^ \t]+)[ \t]+([^ \t]+)")
			list(APPEND pairs "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
		else()
			message(FATAL_ERROR "gen-wadlists: ${PATH}: needs two names on '${line}'")
		endif()
	endforeach()
	set(${OUT_VAR} "${pairs}" PARENT_SCOPE)
endfunction()

zx_read_tokens("${IWAD_LIST}" IWADS)
zx_read_tokens("${SITE_LIST}" SITES)
zx_read_pairs("${SUBST_LIST}" SUBSTS)

list(LENGTH IWADS IWAD_COUNT)
list(LENGTH SITES SITE_COUNT)
if(IWAD_COUNT EQUAL 0)
	message(FATAL_ERROR "gen-wadlists: ${IWAD_LIST} yielded no entries -- refusing to generate an empty allowlist")
endif()
if(SITE_COUNT EQUAL 0)
	message(FATAL_ERROR "gen-wadlists: ${SITE_LIST} yielded no entries")
endif()

set(IWAD_ENTRIES "")
foreach(w IN LISTS IWADS)
	string(APPEND IWAD_ENTRIES "\t\"${w}\",\n")
endforeach()

# Every replacement has to be downloadable in its own right, so checking it against the allowlist here
# turns a silent dud entry (substitute something, then refuse to fetch it) into a build failure.
set(SUBST_ENTRIES "")
set(SUBST_COUNT 0)
list(LENGTH SUBSTS SUBST_FLAT_LEN)
math(EXPR SUBST_LAST "${SUBST_FLAT_LEN} - 1")
foreach(i RANGE 0 ${SUBST_LAST} 2)
	list(GET SUBSTS ${i} from)
	math(EXPR j "${i} + 1")
	list(GET SUBSTS ${j} to)
	# list(FIND) rather than IN_LIST: in script mode (-P) CMP0057 is not set NEW, and IN_LIST is a
	# parse error there rather than a false.
	list(FIND IWADS "${to}" allowIdx)
	if(allowIdx EQUAL -1)
		message(FATAL_ERROR
			"gen-wadlists: ${SUBST_LIST} maps ${from} to ${to}, which is not in ${IWAD_LIST} -- "
			"a substitute has to be an IWAD we are allowed to download")
	endif()
	string(APPEND SUBST_ENTRIES "\t{ \"${from}\", \"${to}\" },\n")
	math(EXPR SUBST_COUNT "${SUBST_COUNT} + 1")
endforeach()

# The mirror list is emitted as ONE space-joined string because that is the shape its consumer wants:
# it is a CVAR default, and cl_fua_downloadsites is a space-separated list a player can edit.
string(REPLACE ";" " " SITE_STRING "${SITES}")

file(WRITE "${OUT}"
"// GENERATED FILE -- do not edit.
//
// Built by tools/gen-wadlists.cmake from the repo-root lists:
//   iwadallowlist.txt     ${IWAD_COUNT} entries
//   waddownloadsites.txt  ${SITE_COUNT} entries
//   iwadsubstitutes.txt   ${SUBST_COUNT} entries
//
// Edit those, not this. See features/wad-download/README.md.

#ifndef ZX_WADDOWNLOAD_LISTS_H
#define ZX_WADDOWNLOAD_LISTS_H

namespace zx {

// const at namespace scope has internal linkage, so including this in more than one TU is fine.
const char *const kFreeIwads[] = {
${IWAD_ENTRIES}};

const char *const kDefaultDownloadSites = \"${SITE_STRING}\";

// { the IWAD a server asked for, the free IWAD we load instead when it is missing }
const char *const kIwadSubstitutes[][2] = {
${SUBST_ENTRIES}};

} // namespace zx

#endif // ZX_WADDOWNLOAD_LISTS_H
")
