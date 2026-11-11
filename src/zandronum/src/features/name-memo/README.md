# Lookups by name remembered by pointer

ACS finds cvars and classes by name on every call. The name table in front of them hashes the string and takes the
table's lock each time, ~150 ns a lookup.

- Complex Doom's effect actors made ~2,300 `GetCVar` calls a tic (0.34 ms).
- DnD's scripts make hundreds of `CheckInventory`, `CheckActorInventory`, `GiveInventory` and `TakeInventory` calls a
  tic. Each resolves its item's class through `PClass::FindClass(const char *)`. Hashing the name and walking the
  class table was ~1% of the playsim on an aimed Minigun lap of Sunder MAP11.

A script passes the same string each call, so each thread now remembers what a name's pointer found.

- A hit is checked against the found object's own name (the cvar's name, the class's `TypeName`), since a pointer can
  come back holding another name.
- Each user keeps its own generation, which every thread's memo then ignores once it moves, so the memo always
  answers as the full lookup would:
  - cvars: an entry added to the name cache, a cvar destroyed, `C_ClearCVarCache`;
  - classes: a class registered (`PClass::InsertIntoHash`), `PClass::StaticInit`, `PClass::StaticShutdown`.
- A class not found is not remembered.
- `computation/namememo_compute.*`: the slots and generations.
- Hooks: `c_cvars.cpp` `FindCVar`, `~FBaseCVar`, `C_ClearCVarCache`; `dobjtype.cpp` `PClass::FindClass (const char *)`,
  `InsertIntoHash`, `StaticInit`, `StaticShutdown`.
