# Cvar lookups remembered by pointer

ACS `GetCVar` finds a cvar by name on every call. The name cache in front of `FindCVar` still hashes the string
and takes the name table's lock, ~150 ns a call; Complex Doom's effect actors made ~2,300 calls a tic (0.34 ms).
A script passes the same string each call, so each thread now remembers what a name's pointer found.

- A hit is checked against the cvar's name, since a pointer can come back holding another name.
- Every change to the name cache (an entry added, a cvar destroyed, `C_ClearCVarCache`) starts a new generation,
  which every thread's memo then ignores, so the memo always answers as the name cache would.
- `computation/cvarmemo_compute.*`: the slots and generations.
- Hooks: `c_cvars.cpp` `FindCVar`, `~FBaseCVar`, `C_ClearCVarCache`.
