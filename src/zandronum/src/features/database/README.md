# database

Keeps the ACS database functions (`SetDBEntry`, `GetDBEntry`, `IncrementDBEntry`, `SortDBEntries`, `GetDBEntryRank`, …) from stalling the tic, and from losing writes, when several servers share one database file.

## Reads and the file

- Each call runs one cached prepared statement (`computation/database_compute.cpp`).
- `fua_ValueOrder`, an index on `(Namespace, CAST(Value AS INTEGER), KeyName)`, lets sort and rank read a namespace in order; ties keep the key order the old sort gave.
- A database file uses WAL while `fua_db_wal` is on (the default) and the rollback journal when it is off; `db_enable_wal` and `db_disable_wal` set that cvar.
- `fua_db_synchronous` (default 2, FULL) makes every commit wait for the disk.
- A thread with its own connection checkpoints the log every second, so that flush never lands on the game thread.

## The write queue (`fua_db_queue`, default on, database files only)

- A write becomes an op in this server's queue; reads see it at once through an overlay (`database_queue_compute.cpp`).
- At each tic's end the tic's ops, or a mod's whole `Begin/EndDBTransaction`, become a batch. The batches are written to this server's outbox, `<databasefile>.fua-outbox-<slot>`, in one transaction with a full flush. Once a tic ends, its writes survive an engine crash and a power cut (`database_outbox_compute.cpp`).
- The outbox is locked to one live process (SQLite's exclusive locking mode), and the lock dies with a crashed one. A starting server takes the first free slot and resumes what that slot still holds. Its writer also replays any other unlocked slot's leftovers at start and every minute.
- A writer thread applies batches to the shared file, one transaction each, waiting out another server's lock instead of dropping the write (`database_writer_compute.cpp`):
  - A set lands only when it is newer than the row and than any delete marker (`fua_Deleted`).
  - A delete leaves a marker, pruned after 30 days.
  - An increment is an addition in SQL, so every server's count lands.
  - `fua_Applied` keeps each server's last applied batch, so a replay never applies one twice.
- Timestamps come from a hybrid logical clock: the later of this machine's clock and the newest stamp seen in the file, in whole 0.1 ms steps, in the `Timestamp` column's own unit. A write made after reading another server's write therefore always orders after it. Synced clocks (NTP) only decide between truly simultaneous writes, and a clock more than a minute behind the file is warned about once.
- `fua_db_status` prints the outbox slot, the server id, what the shared file has yet to take, and the writer's retries and last error.
- Stock Zandronum servers can share the file: they write wall-clock stamps the rule compares, but ignore delete markers.
- One file shared over a network drive is not safe in any mode: docs/database-shared-file-risk.md.

## Engine hooks

- `za_database.cpp`: the ACS-facing `DATABASE_*` calls go through the store when it is open, else straight to `database_compute.cpp`. `DATABASE_Init` applies the pragmas, binds the statements, starts the checkpointer and opens the store. `DATABASE_EndTic` ends the store's tic. `database_ClearHandle` closes the store (draining up to 3 s; the rest waits in the outbox), then the checkpointer and the statements. `DATABASE_EnableWAL`/`DATABASE_DisableWAL` set `fua_db_wal`.
- `g_game.cpp`: `G_TickerEnd` calls `DATABASE_EndTic`, on clients, with the game thread, and on the dedicated server alike.
- `tests/CMakeLists.txt` builds the engine's SQLite into the test binary.
