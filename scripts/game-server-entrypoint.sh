#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Turn a catalogue id into a running, listed ForkUnderA server, with nothing edited.
#
#   docker run --network host -v fua-data:/data ghcr.io/rc4l/forkundera-game-server duel40
#
# WHY THIS IS SHELL AND NOT ENGINE CODE. catalogue/<id>/addon.json is data; the engine is one reader
# of it, not the definition of it. A dedicated host needs the answer to "which files does this entry
# need and do I have them" BEFORE there is an engine process to ask, so the resolution happens out
# here with jq and curl. features/addon-catalogue does the same job for a player at a menu, and
# features/server-hosting owns a child process because a desktop client cannot be both authority and
# client. A container has neither problem: PID 1 is the server.
#
# WHAT IT DELIBERATELY DOES NOT DO. It never parses server.cfg. That goes to the engine with +exec
# exactly as written, which is the whole reason the catalogue reused Zandronum's own config format:
# an operator with strong opinions writes the cfg they already know how to write, and we cannot get
# it wrong because we never read it.
set -euo pipefail

readonly INSTALL_DIR=/opt/forkundera
readonly DATA_DIR=/data

# [rc4l] Content-addressed, mirroring features/wad-download's by-hash store and for the same reasons
# (see computation/wadstore_compute.h): two different files called test.wad coexist, nothing is ever
# clobbered, and resolution is a path build rather than a search that can find the wrong copy.
#
# The reason it matters HERE is different and is about hosting ten servers on one box: point every
# container at one /data volume and a 200 MB mod fetched for the first server is already present for
# the other nine. Keyed by name as well as digest, because a digest alone would collapse two files
# that collide into one entry.
readonly STORE_DIR="${DATA_DIR}/wads/by-md5"

readonly DEFAULT_PORT=10666

# [rc4l] ALL THREE WRITE TO STDERR, and that is load-bearing rather than tidy.
#
# resolve_file and fetch_file return a path by printing it, so their caller reads them through $( ).
# Command substitution captures stdout and does not care what was meant to be a message: with log()
# on stdout, every "fetching..." line was concatenated into the path, and the engine received a
# multi-line -file argument that matched no file. It loaded neither PWAD, said nothing about it, and
# came up on the substituted IWAD's own map01 looking perfectly healthy.
#
# So stdout belongs to return values here and nothing else. Docker captures both streams, so nothing
# is lost from the log.
log()  { printf '[fua-entry] %s\n' "$*" >&2; }
warn() { printf '[fua-entry] WARNING: %s\n' "$*" >&2; }
die()  { printf '[fua-entry] ERROR: %s\n' "$*" >&2; exit 1; }

#---------------------------------------------------------------------------------------------------
# Argument safety
#
# [rc4l] Deliberately the same rules as the engine's own hostargs_compute.cpp, and deliberately
# REFUSING rather than escaping. There is no legitimate map called `-iwad`, so a value that could be
# read as a flag is a mistake or an attack, and quietly quoting it would let both through. Values here
# come from a json file that may have been dropped into /data/catalogue by whoever runs this box.
#---------------------------------------------------------------------------------------------------

# Rejects empty, anything a parser would read as the next flag, and anything carrying a control
# character, quote or backslash.
is_safe_value() {
	local value="$1"
	[ -n "${value}" ] || return 1
	case "${value}" in
		-*|+*) return 1 ;;
	esac
	case "${value}" in
		*[[:cntrl:]]*|*'"'*|*'\'*) return 1 ;;
	esac
	return 0
}

# A filename we are willing to build a path out of: no separators, no traversal, no drive letters.
is_bare_name() {
	local name="$1"
	is_safe_value "${name}" || return 1
	case "${name}" in
		*/*|*:*|.|..|*..*) return 1 ;;
	esac
	return 0
}

is_hex_md5() {
	printf '%s' "$1" | grep -Eqi '^[0-9a-f]{32}$'
}

# [rc4l] Which Freedoom phase stands in for a game, mirroring iwadsubstitutes.txt.
#
# Phase 2 was the only answer here for a long time, which was wrong for the Doom 1 half of that
# table: an entry wanting doom.wad was handed Doom II's data, so the episode structure and half the
# textures were not what the maps expected. Phase 1 is the stand-in for those.
free_stand_in() {
	case "$(printf '%s' "$1" | tr 'A-Z' 'a-z')" in
		doom.wad|doom1.wad|doomu.wad|bfgdoom.wad|doombfg.wad) printf 'freedoom1.wad' ;;
		*) printf 'freedoom2.wad' ;;
	esac
}

#---------------------------------------------------------------------------------------------------
# Configuration
#---------------------------------------------------------------------------------------------------

# [rc4l] `docker run <image> duel40` is the shortest thing that works, so a bare first argument is the
# entry. Anything starting with - or + is passed to the engine instead, which keeps an escape hatch
# for the operator who needs one flag we never thought of, without turning this into forty env vars.
ENTRY_ID="${FUA_ENTRY:-}"
PASSTHROUGH_ARGS=()
if [ $# -gt 0 ]; then
	case "$1" in
		-*|+*) PASSTHROUGH_ARGS=( "$@" ) ;;
		*)     ENTRY_ID="$1"; shift; PASSTHROUGH_ARGS=( "$@" ) ;;
	esac
fi

PORT="${FUA_PORT:-${DEFAULT_PORT}}"
# [rc4l] NOT DEFAULTED, deliberately. These become `+cvar` arguments, and the engine applies `+`
# arguments left to right AFTER the entry's `+exec`, so anything given a default here would silently
# beat whatever the entry's server.cfg says. Defaulting FUA_PLAYERS to 8 meant a cfg saying
# sv_maxplayers 16 was overruled by a number nobody typed, and the cfg looked broken.
#
# Unset now means "say nothing", so the cfg decides, and failing that the engine's own default does.
# Everything expressible as a CVAR belongs in server.cfg; these exist only to override it on purpose.
PLAYERS="${FUA_PLAYERS-}"
ANNOUNCE="${FUA_ANNOUNCE-}"
SERVE_WADS="${FUA_SERVE_WADS-}"
SUBSTITUTE_IWAD="${FUA_IWAD_SUBSTITUTE:-1}"
MAX_MB="${FUA_MAX_FILE_MB:-2048}"

SERVER_DIR="${FUA_SERVER_DIR-}"

if [ -z "${SERVER_DIR}" ] && [ -z "${ENTRY_ID}" ]; then
	die "nothing to host. Give a catalogue id, or point FUA_SERVER_DIR at a folder holding server.cfg and wads.txt"
fi
if [ -z "${SERVER_DIR}" ]; then
	is_bare_name "${ENTRY_ID}" || die "refusing catalogue id '${ENTRY_ID}': not a plain name"
fi

case "${PORT}" in
	''|*[!0-9]*) die "FUA_PORT must be a number, got '${PORT}'" ;;
esac
# [rc4l] Same floor as the engine's IsUsablePort: nothing below 1024 binds without elevation, and this
# container runs as an unprivileged user on purpose.
[ "${PORT}" -ge 1024 ] && [ "${PORT}" -le 65535 ] || die "FUA_PORT ${PORT} is outside 1024-65535"

#---------------------------------------------------------------------------------------------------
# Folder mode
#
# [rc4l] A server is a folder holding two files and nothing else:
#
#     servers/duel40/server.cfg    how it plays -- every cvar, including sv_hostname
#     servers/duel40/wads.txt      what to load, in order
#
# wads.txt reads like the command line it becomes:
#
#     iwad doom2.wad
#     file duel40b.pk3
#     file zandrospree2rc2.pk3
#
# WHY THIS EXISTS ALONGSIDE THE CATALOGUE. A cfg cannot name a wad -- there is no cvar for it, only
# -iwad and -file -- so a folder of cfgs would be settings with no game. One list of files is the
# minimum extra information, and this is the version of it for an operator who already has the wads:
# no hashes, no schema, no downloading, nothing to keep in step with the engine.
#
# The catalogue keeps its own reason to exist: hashes are what make fetching from a stranger's mirror
# safe, and an entry is shared with the client's HOST tab. Neither mode is the other's replacement.
#---------------------------------------------------------------------------------------------------

FOLDER_IWAD=""
FOLDER_FILES=()

if [ -n "${SERVER_DIR}" ]; then
	case "${SERVER_DIR}" in
		*..*) die "refusing server dir '${SERVER_DIR}': contains .." ;;
	esac
	[ -d "${SERVER_DIR}" ] || die "no such server folder: ${SERVER_DIR}"

	readonly WADS_TXT="${SERVER_DIR}/wads.txt"
	[ -f "${WADS_TXT}" ] || die "${SERVER_DIR} has no wads.txt, so nothing says which wads to load"

	line_no=0
	while IFS= read -r line || [ -n "${line}" ]; do
		line_no=$(( line_no + 1 ))
		# Strip comments and surrounding whitespace; skip what is left of a blank line.
		line="${line%%#*}"
		line="$(printf '%s' "${line}" | tr -d '
' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
		[ -n "${line}" ] || continue

		kind="${line%% *}"
		name="${line#* }"
		name="$(printf '%s' "${name}" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"

		[ "${kind}" != "${name}" ] || die "${WADS_TXT}:${line_no}: expected 'iwad <file>' or 'file <file>', got '${line}'"
		is_bare_name "${name}" || die "${WADS_TXT}:${line_no}: '${name}' is not a plain filename"

		case "${kind}" in
			iwad)
				[ -z "${FOLDER_IWAD}" ] || die "${WADS_TXT}:${line_no}: a second iwad line; a server loads exactly one"
				FOLDER_IWAD="${name}"
				;;
			file)
				FOLDER_FILES+=( "${name}" )
				;;
			*)
				die "${WADS_TXT}:${line_no}: unknown keyword '${kind}' -- only 'iwad' and 'file' exist"
				;;
		esac
	done < "${WADS_TXT}"

	[ -n "${FOLDER_IWAD}" ] || die "${WADS_TXT} names no iwad, and a server cannot start without one"

	log "server: ${SERVER_DIR}  (${#FOLDER_FILES[@]} pwad(s))"
fi

# [rc4l] Same rule as the other cvars: only an explicit FUA_NAME becomes +sv_hostname, so the cfg's
# own sv_hostname is not overruled by a default nobody chose. Set for BOTH modes -- it lived inside
# the catalogue branch at first, which left folder mode tripping over it unset.
SERVER_NAME="${FUA_NAME-}"

# [rc4l] Catalogue mode only. Folder mode already knows its files and its cfg.
if [ -z "${SERVER_DIR}" ]; then
#---------------------------------------------------------------------------------------------------
# Locate the entry
#---------------------------------------------------------------------------------------------------

# [rc4l] The operator's own entries win over the shipped ones, which is what makes "bring your own
# mod" work without a rebuild: drop a folder in /data/catalogue/<id>/ and it shadows ours by id.
ENTRY_DIR=""
for candidate in "${DATA_DIR}/catalogue/${ENTRY_ID}" "${INSTALL_DIR}/catalogue/${ENTRY_ID}"; do
	if [ -f "${candidate}/addon.json" ]; then
		ENTRY_DIR="${candidate}"
		break
	fi
done

if [ -z "${ENTRY_DIR}" ]; then
	log "known entries:"
	for dir in "${INSTALL_DIR}"/catalogue/*/ "${DATA_DIR}"/catalogue/*/; do
		[ -f "${dir}addon.json" ] || continue
		printf '    %s\n' "$(basename "${dir}")"
	done
	die "no catalogue entry called '${ENTRY_ID}'"
fi

readonly MANIFEST="${ENTRY_DIR}/addon.json"
jq -e . "${MANIFEST}" >/dev/null 2>&1 || die "${MANIFEST} is not valid json"

ENTRY_NAME="$(jq -r '.name // ""' "${MANIFEST}")"
ENTRY_IWAD="$(jq -r '.iwad // ""' "${MANIFEST}")"
ENTRY_MAP="$(jq -r '.map // ""' "${MANIFEST}")"

#---------------------------------------------------------------------------------------------------
# Variants
#
# [rc4l] An entry can offer several ways to play the same thing -- Duel 40 and Duel 32, Skulltag's
# deathmatch and its CTF -- and each names its own cfg, its own opening map and its own extra files.
#
# A variant ADDS to the entry's file list, never replaces it (see addonfile_compute.h). The shared
# list holds what every variant needs; the variant holds only what is peculiar to it. Restating the
# shared files inside each variant would mean copies, and copies drift: update the base, miss one
# variant, and that variant quietly loads something else.
#
# Missing this cost a live server its content. The entrypoint predated variants, read only the
# top-level files[], and hosted Duel 40 with the announcer pk3 and none of its maps -- on the
# substituted IWAD's own map01, looking perfectly healthy.
#---------------------------------------------------------------------------------------------------

VARIANT_ID="${FUA_VARIANT-}"
VARIANT_JSON="null"

if [ "$(jq '(.variants // []) | length' "${MANIFEST}")" -gt 0 ]; then
	if [ -n "${VARIANT_ID}" ]; then
		VARIANT_JSON="$(jq -c --arg v "${VARIANT_ID}" 'first(.variants[] | select(.id == $v)) // null' "${MANIFEST}")"
		if [ "${VARIANT_JSON}" = "null" ]; then
			log "variants offered by ${ENTRY_ID}:"
			jq -r '.variants[] | "    \(.id)	\(.name // "")"' "${MANIFEST}" >&2
			die "no variant '${VARIANT_ID}' in entry '${ENTRY_ID}'"
		fi
	else
		# The one marked default, else the first. An entry with variants always has something to pick,
		# so falling through to "no variant" would silently host the shared files alone -- which is
		# exactly the failure this section exists to prevent.
		VARIANT_JSON="$(jq -c 'first(.variants[] | select(.default == true)) // .variants[0]' "${MANIFEST}")"
	fi

	VARIANT_ID="$(printf '%s' "${VARIANT_JSON}" | jq -r '.id // ""')"
	VARIANT_NAME="$(printf '%s' "${VARIANT_JSON}" | jq -r '.name // ""')"
	# Both fall back to the entry's, which is why they are read after it.
	VARIANT_MAP="$(printf '%s' "${VARIANT_JSON}" | jq -r '.map // ""')"
	VARIANT_CFG="$(printf '%s' "${VARIANT_JSON}" | jq -r '.cfg // ""')"
	[ -n "${VARIANT_MAP}" ] && ENTRY_MAP="${VARIANT_MAP}"
	log "variant: ${VARIANT_ID}  (${VARIANT_NAME:-unnamed})"
fi

log "entry:  ${ENTRY_ID}  (${ENTRY_NAME:-unnamed})  from ${ENTRY_DIR}"


fi
#---------------------------------------------------------------------------------------------------
# Mirrors
#---------------------------------------------------------------------------------------------------

read_mirrors() {
	if [ -n "${FUA_MIRRORS:-}" ]; then
		printf '%s\n' ${FUA_MIRRORS}
		return
	fi
	# The shipped list is "<base url>   <who runs it>", so take the first field and drop comments.
	awk '!/^[[:space:]]*#/ && NF { print $1 }' "${INSTALL_DIR}/waddownloadsites.txt"
}

mapfile -t MIRRORS < <(read_mirrors)
[ "${#MIRRORS[@]}" -gt 0 ] || warn "no download mirrors configured -- missing files cannot be fetched"

#---------------------------------------------------------------------------------------------------
# Finding and fetching files
#---------------------------------------------------------------------------------------------------

# Where a file with this digest and name lives, whether or not it is there yet.
store_path() { printf '%s/%s/%s' "${STORE_DIR}" "$(printf '%s' "$1" | tr 'A-Z' 'a-z')" "$2"; }

md5_of() { md5sum "$1" | cut -d' ' -f1; }

# Somewhere this file already is: the store first, then loose files an operator dropped in /data/wads,
# then the image's own payload (which is where freedoom2.wad and the pk3s live).
find_local() {
	local name="$1" md5="$2" path

	if [ -n "${md5}" ]; then
		path="$(store_path "${md5}" "${name}")"
		[ -f "${path}" ] && { printf '%s' "${path}"; return 0; }
	fi

	for path in "${DATA_DIR}/wads/${name}" "${INSTALL_DIR}/${name}"; do
		if [ -f "${path}" ]; then
			# [rc4l] A loose file that does not match is not "found". Having a file by that name is not
			# the same as having THAT file: the engine authenticates lumps on join, so serving
			# yesterday's copy fails at the client with a message about nothing that is actually wrong.
			if [ -n "${md5}" ] && [ "$(md5_of "${path}")" != "$(printf '%s' "${md5}" | tr 'A-Z' 'a-z')" ]; then
				warn "${path} does not match the entry's md5 -- ignoring it"
				continue
			fi
			printf '%s' "${path}"
			return 0
		fi
	done

	return 1
}

# [rc4l] Three spellings per mirror, which is not padding: wads.doomleague.org serves AV.WAD and 404s
# av.wad, where most other mirrors do the reverse. Same reason the engine's shipped list carries them.
fetch_file() {
	local name="$1" md5="$2" dest tmp url got
	local lower upper

	lower="$(printf '%s' "${name}" | tr 'A-Z' 'a-z')"
	upper="$(printf '%s' "${name}" | tr 'a-z' 'A-Z')"

	dest="$(store_path "${md5}" "${name}")"
	mkdir -p "$(dirname "${dest}")"
	tmp="${dest}.part"

	for base in "${MIRRORS[@]}"; do
		for spelling in "${name}" "${lower}" "${upper}"; do
			url="${base%/}/${spelling}"
			log "  trying ${url}"

			if ! curl -fsSL \
					--connect-timeout 10 --max-time 1800 \
					--max-filesize "$(( MAX_MB * 1024 * 1024 ))" \
					-o "${tmp}" "${url}" 2>/dev/null; then
				rm -f "${tmp}"
				continue
			fi

			got="$(md5_of "${tmp}")"
			if [ "${got}" != "$(printf '%s' "${md5}" | tr 'A-Z' 'a-z')" ]; then
				# [rc4l] Fall through to the next mirror rather than failing: a bad copy on one site
				# should not deny a file the next site has correctly. Only an exhausted list is an error.
				warn "  ${url} served md5 ${got}, entry wants ${md5} -- trying elsewhere"
				rm -f "${tmp}"
				continue
			fi

			mv "${tmp}" "${dest}"
			log "  got ${name} (${got})"
			printf '%s' "${dest}"
			return 0
		done
	done

	return 1
}

resolve_file() {
	local name="$1" md5="$2" path

	if path="$(find_local "${name}" "${md5}")"; then
		printf '%s' "${path}"
		return 0
	fi

	# [rc4l] No digest means no way to tell what a mirror actually sent, so we refuse to fetch rather
	# than load something unverified. An entry that wants this file must ship its md5.
	if ! is_hex_md5 "${md5}"; then
		die "entry needs '${name}' but ships no md5 for it, so it cannot be fetched safely"
	fi

	log "  ${name} not present, fetching"
	fetch_file "${name}" "${md5}" || die "no mirror had '${name}' with md5 ${md5}"
}

#---------------------------------------------------------------------------------------------------
# What folder mode supplies in place of a manifest
#---------------------------------------------------------------------------------------------------

if [ -n "${SERVER_DIR}" ]; then
	ENTRY_IWAD="${FOLDER_IWAD}"
	ENTRY_MAP=""			# the cfg's rotation decides; a folder has nothing else to say
	ENTRY_NAME=""
	ENTRY_ID="$(basename "${SERVER_DIR}")"
fi

#---------------------------------------------------------------------------------------------------
# Resolve the IWAD
#---------------------------------------------------------------------------------------------------

IWAD_PATH=""
if [ -n "${ENTRY_IWAD}" ]; then
	is_bare_name "${ENTRY_IWAD}" || die "entry names an unusable iwad '${ENTRY_IWAD}'"

	if IWAD_PATH="$(find_local "${ENTRY_IWAD}" "")"; then
		log "iwad:   ${IWAD_PATH}"
	elif [ "${SUBSTITUTE_IWAD}" = "1" ] && [ -f "${INSTALL_DIR}/$(free_stand_in "${ENTRY_IWAD}")" ]; then
		# [rc4l] Never fetched, always substituted. Every IWAD is assumed commercial (see
		# features/wad-download/README.md), and a container has no way to know what its operator owns.
		SUBSTITUTE="$(free_stand_in "${ENTRY_IWAD}")"
		IWAD_PATH="${INSTALL_DIR}/${SUBSTITUTE}"
		warn "${ENTRY_IWAD} is not on this box; hosting on ${SUBSTITUTE} instead."
		warn "  This works for a PWAD that replaces every map. On STOCK maps the geometry differs and"
		warn "  joining clients will fail level authentication. Put ${ENTRY_IWAD} in /data/wads to fix."
	else
		die "entry needs ${ENTRY_IWAD}; put it in /data/wads (IWADs are never downloaded)"
	fi
fi

#---------------------------------------------------------------------------------------------------
# Resolve the PWADs, in the entry's load order
#---------------------------------------------------------------------------------------------------

PWAD_PATHS=()

if [ -n "${SERVER_DIR}" ]; then
	for fname in ${FOLDER_FILES[@]+"${FOLDER_FILES[@]}"}; do
		# [rc4l] No digest, so nothing may be fetched: a file we cannot verify is one we will not
		# download. Missing is a hard stop rather than a skip, because a server quietly missing a wad
		# is the failure this whole project keeps rediscovering.
		if ! resolved="$(find_local "${fname}" "")"; then
			die "${fname} is not in ${DATA_DIR}/wads (folder mode never downloads -- put it there)"
		fi
		PWAD_PATHS+=( "${resolved}" )
	done
fi

if [ -z "${SERVER_DIR}" ]; then
# [rc4l] Shared first, then the variant's, which is the order they are declared and the order the
# engine will load them in.
ALL_FILES="$(jq -c --argjson v "${VARIANT_JSON}" '(.files // []) + (($v.files) // [])' "${MANIFEST}")"
file_count="$(printf '%s' "${ALL_FILES}" | jq 'length')"
for i in $(seq 0 $(( file_count - 1 )) ); do
	[ "${file_count}" -gt 0 ] || break

	fname="$(printf '%s' "${ALL_FILES}" | jq -r ".[${i}].name // \"\"")"
	fmd5="$(printf '%s' "${ALL_FILES}" | jq -r ".[${i}].md5 // \"\"")"

	is_bare_name "${fname}" || die "entry names an unusable file '${fname}'"

	resolved="$(resolve_file "${fname}" "${fmd5}")"

	# [rc4l] Check what came back is a single path to a file that exists, and DIE rather than skip.
	#
	# This is the guard the stdout bug walked straight past. resolve_file returns through stdout, so
	# anything else written there ends up in this string; the engine then took a -file argument that
	# named nothing, loaded neither PWAD, and came up looking healthy on the substituted IWAD's own
	# first map. A server quietly running the wrong content is worse than one that refuses to start,
	# because only the second kind gets noticed.
	case "${resolved}" in
		*$'\n'*) die "internal: resolved path for '${fname}' contains newlines -- something wrote to stdout" ;;
	esac
	is_safe_value "${resolved}" || die "internal: unusable resolved path for '${fname}'"
	[ -f "${resolved}" ] || die "internal: resolved '${fname}' to '${resolved}', which is not a file"

	PWAD_PATHS+=( "${resolved}" )
done

fi

#---------------------------------------------------------------------------------------------------
# Build the command line
#
# [rc4l] Order matches the engine's own BuildHostArgs, including the rule that +exec is the FIRST '+'
# argument. The engine applies these left to right, so the entry's cfg going first is what makes the
# operator's settings beat the experience: an entry describes what to play, the host decides how to
# run it. A cfg carrying sv_maxplayers would otherwise quietly overrule the number set here.
#---------------------------------------------------------------------------------------------------

ARGS=( "${INSTALL_DIR}/forkundera-server" -host )

[ -n "${IWAD_PATH}" ] && ARGS+=( -iwad "${IWAD_PATH}" )
for path in ${PWAD_PATHS[@]+"${PWAD_PATHS[@]}"}; do
	ARGS+=( -file "${path}" )
done

# [rc4l] The variant names its cfg; server.cfg is the fallback for an entry that has no variants.
if [ -n "${SERVER_DIR}" ]; then
	ENTRY_DIR="${SERVER_DIR}"
	VARIANT_CFG="server.cfg"
fi

CFG_NAME="${VARIANT_CFG:-server.cfg}"
is_bare_name "${CFG_NAME}" || die "variant names an unusable cfg '${CFG_NAME}'"

if [ -f "${ENTRY_DIR}/${CFG_NAME}" ]; then
	ARGS+=( +exec "${ENTRY_DIR}/${CFG_NAME}" )
	log "cfg:    ${ENTRY_DIR}/${CFG_NAME}"
elif [ -z "${ENTRY_MAP}" ]; then
	# Nothing to pick a map for us and the entry named none, so the engine needs somewhere to start.
	ENTRY_MAP="map01"
fi

if [ -n "${ENTRY_MAP}" ]; then
	is_safe_value "${ENTRY_MAP}" && ARGS+=( +map "${ENTRY_MAP}" )
fi

ARGS+=( -port "${PORT}" )
if [ -n "${SERVER_NAME}" ] && is_safe_value "${SERVER_NAME}"; then
	ARGS+=( +sv_hostname "${SERVER_NAME}" )
fi
if [ -n "${PLAYERS}" ]; then
	case "${PLAYERS}" in
		''|*[!0-9]*) die "FUA_PLAYERS must be a number, got '${PLAYERS}'" ;;
	esac
	ARGS+=( +sv_maxclients "${PLAYERS}" +sv_maxplayers "${PLAYERS}" )
fi

# A password only counts if it is enforced; setting one without the flag produces a server that looks
# locked in the browser and lets anybody in.
if [ -n "${FUA_PASSWORD:-}" ] && is_safe_value "${FUA_PASSWORD}"; then
	ARGS+=( +sv_password "${FUA_PASSWORD}" +sv_forcepassword 1 )
fi
if [ -n "${FUA_JOIN_PASSWORD:-}" ] && is_safe_value "${FUA_JOIN_PASSWORD}"; then
	ARGS+=( +sv_joinpassword "${FUA_JOIN_PASSWORD}" +sv_forcejoinpassword 1 )
fi
if [ -n "${FUA_RCON:-}" ] && is_safe_value "${FUA_RCON}"; then
	ARGS+=( +sv_rconpassword "${FUA_RCON}" )
else
	warn "no FUA_RCON set -- this server cannot be administered remotely"
fi

if [ -n "${FUA_REGISTRY:-}" ] && is_safe_value "${FUA_REGISTRY}"; then
	ARGS+=( +fua_serverregistry_host "${FUA_REGISTRY}" )
fi
if [ -n "${ANNOUNCE}" ]; then
	ARGS+=( +sv_fua_serverregistry_announce "${ANNOUNCE}" )
fi
if [ -n "${SERVE_WADS}" ]; then
	ARGS+=( +sv_fua_download "${SERVE_WADS}" )
fi

ARGS+=( ${PASSTHROUGH_ARGS[@]+"${PASSTHROUGH_ARGS[@]}"} )

#---------------------------------------------------------------------------------------------------
# Go
#---------------------------------------------------------------------------------------------------

log "name:   ${SERVER_NAME:-(from server.cfg, or the engine default)}"
log "port:   ${PORT} (UDP for the game, TCP for downloads -- both must reach this container)"
if [ "${ANNOUNCE}" != "0" ]; then
	# [rc4l] The registry lists a server at the source address of its own announce packet, so any NAT
	# between here and it lists this server somewhere nothing is listening. Host networking is the fix
	# and there is no other one; see Dockerfile.game-server.
	log "announcing to the registry. If this container is not on host networking, the listing will be wrong."
	log "reachability is decided by the registry, not by us: it answers by sending an unsolicited"
	log "  packet from outside, so watch the log for whether one arrives."
fi

log "exec:   ${ARGS[*]}"
exec "${ARGS[@]}"
