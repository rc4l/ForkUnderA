# database

Keeps the ACS database functions (`SetDBEntry`, `GetDBEntry`, `IncrementDBEntry`, `SortDBEntries`, `GetDBEntryRank`, …) from stalling the tic.

- Each call runs one cached prepared statement: an upsert in place of an exists check plus a write, and a single query for rank.
- `fua_ValueOrder`, an index on `(Namespace, CAST(Value AS INTEGER), KeyName)`, lets sort and rank read a namespace in order instead of scanning it; ties keep the key order the old sort gave.
- A database file uses WAL while `fua_db_wal` is on (the default), so a commit is one log write, and the rollback journal when it is off; `db_enable_wal` and `db_disable_wal` now set that cvar, so the last setting applied wins whether it comes before or after the file opens.
- `fua_db_synchronous` (default 2, FULL) keeps every commit durable; 1 (NORMAL) skips the per-commit flush and can lose the last commits on a power cut, never on an engine crash.
- A thread with its own connection checkpoints the log every second, and SQLite's automatic checkpoints are off, so their disk flush never lands on the game thread.

## Engine hooks

- `za_database.cpp`: the ACS-facing `DATABASE_Save*`, `DATABASE_GetEntryRank`, `DATABASE_GetSortedEntries` and `DATABASE_GetEntries` call `computation/database_compute.cpp`; `DATABASE_Init` applies the pragmas, binds the statements and starts the checkpointer; `DATABASE_CreateTable` creates the index; `database_ClearHandle` stops the checkpointer and finalizes the statements before closing; `DATABASE_EnableWAL`/`DATABASE_DisableWAL` set `fua_db_wal`.
- `tests/CMakeLists.txt` builds the engine's SQLite into the test binary.
