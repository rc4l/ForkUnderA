# Sourced by the commit-tracker wrappers: sets the PY array to a working Python 3.
# [rc4l] On Windows `python3` is often the Microsoft Store stub, which prints nothing and fails; try the real ones in turn.
PY=()
for _c in "${PYTHON:-}" python3 python "py -3"; do
	[ -n "$_c" ] || continue
	read -ra _try <<< "$_c"
	[ "$_c" = "${PYTHON:-}" ] && _try=("$_c")
	if "${_try[@]}" -c 'import sys; sys.exit(sys.version_info < (3, 7))' >/dev/null 2>&1; then PY=("${_try[@]}"); break; fi
done
unset _c _try
[ ${#PY[@]} -gt 0 ] || { echo "commit-tracker: no Python 3 found (set PYTHON)" >&2; exit 1; }
