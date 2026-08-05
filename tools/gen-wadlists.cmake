# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Compiles the repo-root WAD lists into one C++ header, so those files are the single source of
# truth and cannot drift from hand-maintained arrays:
#
#   iwadallowlist.txt     filenames that may be downloaded as IWADs
#   iwadhashes.txt        the SHA-256 builds we vouch for -- the gate
#   iwadsubstitutes.txt   what Freedoom stands in for
#   waddownloadsites.txt  default mirrors
#   waddirectories.txt    where other Doom tools keep WADs (filtered to PLATFORM)
#
# Generated at BUILD time rather than checked in, and read from the binary rather than from disk at
# runtime, because the allowlist is a legal gate: it has to be something we shipped, not something a
# player or a mod can append "doom2.wad" to. A pull request is the only way to add a line, which is
# the right amount of friction for a claim about someone else's licence.
#
# Usage:
#   cmake -DIWAD_LIST=<path> -DSITE_LIST=<path> -DSUBST_LIST=<path> -DHASH_LIST=<path> \
#         -DDIR_LIST=<path> -DPLATFORM=windows|macos|linux -DOUT=<path> \
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

# The well-known WAD directories are pipe-separated rather than whitespace-separated, because real
# paths contain spaces ("Application Support", "My Games"). Only entries tagged with this build's
# platform are collected, so no runtime filtering is needed.
function(zx_read_dirs PATH PLATFORM OUT_VAR)
	set(dirs "")
	if(NOT EXISTS "${PATH}")
		message(FATAL_ERROR "gen-wadlists: missing input ${PATH}")
	endif()
	file(STRINGS "${PATH}" lines)
	foreach(line IN LISTS lines)
		string(STRIP "${line}" line)
		if(line STREQUAL "" OR line MATCHES "^#")
			continue()
		endif()
		if(NOT line MATCHES "^([^|]+)\\|([^|]+)")
			message(FATAL_ERROR "gen-wadlists: ${PATH}: expected '<platform> | <path> | <note>' on '${line}'")
		endif()
		string(STRIP "${CMAKE_MATCH_1}" entryPlatform)
		string(STRIP "${CMAKE_MATCH_2}" entryPath)
		string(TOLOWER "${entryPlatform}" entryPlatform)
		if(NOT entryPlatform MATCHES "^(windows|macos|linux)$")
			message(FATAL_ERROR "gen-wadlists: ${PATH}: '${entryPlatform}' is not windows/macos/linux")
		endif()
		if(entryPlatform STREQUAL "${PLATFORM}")
			list(APPEND dirs "${entryPath}")
		endif()
	endforeach()
	set(${OUT_VAR} "${dirs}" PARENT_SCOPE)
endfunction()

zx_read_tokens("${IWAD_LIST}" IWADS)
zx_read_tokens("${SITE_LIST}" SITES)
zx_read_pairs("${SUBST_LIST}" SUBSTS)
# Same two-token shape as the substitutes: <sha256> <filename>.
zx_read_pairs("${HASH_LIST}" HASHES)
zx_read_dirs("${DIR_LIST}" "${PLATFORM}" WADDIRS)

list(LENGTH IWADS IWAD_COUNT)
list(LENGTH SITES SITE_COUNT)
if(IWAD_COUNT EQUAL 0)
	message(FATAL_ERROR "gen-wadlists: ${IWAD_LIST} yielded no entries -- refusing to generate an empty allowlist")
endif()
if(SITE_COUNT EQUAL 0)
	message(FATAL_ERROR "gen-wadlists: ${SITE_LIST} yielded no entries")
endif()

set(DIR_ENTRIES "")
foreach(d IN LISTS WADDIRS)
	string(APPEND DIR_ENTRIES "\t\"${d}\",\n")
endforeach()
list(LENGTH WADDIRS DIR_COUNT)
if(DIR_COUNT EQUAL 0)
	# Zero-length arrays do not compile; kKnownWadDirCount is 0 so the sentinel is never read.
	set(DIR_ENTRIES "\t\"\",\n")
endif()

set(IWAD_ENTRIES "")
foreach(w IN LISTS IWADS)
	string(APPEND IWAD_ENTRIES "\t\"${w}\",\n")
endforeach()

# [rc4l] The IWAD hash allowlist -- the actual gate. Two things are checked here rather than trusted,
# because a wrong line in this file is a licensing problem rather than a compile error:
#   - the digest is exactly 64 hex characters, so a truncated or MD5 entry cannot sit in the table
#     looking plausible and matching nothing (or worse, matching a truncated computed digest);
#   - the filename is on the IWAD allowlist, so a hash cannot smuggle in a name the name gate refuses.
set(HASH_ENTRIES "")
set(HASH_COUNT 0)
list(LENGTH HASHES HASH_FLAT_LEN)
if(HASH_FLAT_LEN GREATER 0)
	math(EXPR HASH_LAST "${HASH_FLAT_LEN} - 1")
	foreach(i RANGE 0 ${HASH_LAST} 2)
		list(GET HASHES ${i} digest)
		math(EXPR j "${i} + 1")
		list(GET HASHES ${j} fname)
		string(TOLOWER "${digest}" digest)
		# Length checked separately from the character class: CMake's regex engine has no {n}
		# repetition, so "^[0-9a-f]{64}$" silently matches nothing and would reject every valid line.
		string(LENGTH "${digest}" digestLen)
		if(NOT digestLen EQUAL 64 OR NOT digest MATCHES "^[0-9a-f]+$")
			message(FATAL_ERROR
				"gen-wadlists: ${HASH_LIST}: '${digest}' is not a 64-character hex SHA-256 "
				"(for ${fname}) -- got ${digestLen} characters")
		endif()
		list(FIND IWADS "${fname}" hashAllowIdx)
		if(hashAllowIdx EQUAL -1)
			message(FATAL_ERROR
				"gen-wadlists: ${HASH_LIST} vouches for ${fname}, which is not in ${IWAD_LIST} -- "
				"a hash cannot admit a filename the allowlist refuses")
		endif()
		string(APPEND HASH_ENTRIES "\t{ \"${digest}\", \"${fname}\" },\n")
		math(EXPR HASH_COUNT "${HASH_COUNT} + 1")
	endforeach()
endif()
if(HASH_COUNT EQUAL 0)
	# A zero-length array does not compile, and an empty hash list is a legitimate (fully closed)
	# state -- no IWAD is downloadable. Emit an unmatchable sentinel; kFreeIwadHashCount is 0, so the
	# lookup never reads it.
	set(HASH_ENTRIES "\t{ \"\", \"\" },\n")
endif()

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
//   iwadhashes.txt        ${HASH_COUNT} entries
//   waddirectories.txt    ${DIR_COUNT} entries for ${PLATFORM}
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

// { lowercase hex SHA-256, the filename that build is }. The gate: IWAD-magic bytes are kept only if
// their digest is in here. An empty table means no IWAD is downloadable, which is a safe state.
const char *const kFreeIwadHashes[][2] = {
${HASH_ENTRIES}};
const int kFreeIwadHashCount = ${HASH_COUNT};

// Well-known WAD folders belonging to other Doom tools, for THIS platform only. Unexpanded -- they
// carry $VAR and ~ for NicePath to resolve at runtime.
const char *const kKnownWadDirs[] = {
${DIR_ENTRIES}};
const int kKnownWadDirCount = ${DIR_COUNT};

} // namespace zx

#endif // ZX_WADDOWNLOAD_LISTS_H
")
