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
- Reads see this server's pending writes exactly once. Each op keeps its batch number, and a read that depends on the file reads it in one transaction with `fua_Applied`'s number for this server, applying only the ops the file does not hold yet. A key with a pending set or delete needs no file at all.
- Increments follow SQLite's own arithmetic, a step at a time, including the REAL it stores once a sum leaves 64 bits.
- A null namespace or key (an invalid ACS string id) is the empty string, queued or direct.
- A mod transaction ends when its level does (`DATABASE_LevelScriptsEnding`, before the old level's scripts are unloaded), or after a minute of tics, with its writes kept and a console line, so a script killed mid-save cannot hold writes back.
- A failed outbox write is retried every tic until it lands; a writer that makes no progress for 10 s, a mod transaction closed for it, and an outbox file that cannot be opened (at the claim or in a later scan) are each named on the console once.
- On open, leftovers the file already holds (a server killed between applying and pruning) are dropped, not shown or sent again.
- `fua_db_status` prints the outbox slot, the server id, what the shared file has yet to take, and the writer's retries and last error.
- Stock Zandronum servers can share the file: they write wall-clock stamps the rule compares, and the `fua_MarkStockDelete` trigger, which lives in the file and so runs for every connection, gives their deletes a marker stamped at the time of the delete. The queue writer's own transactions hold a row in `fua_Writing` that never commits, and the trigger skips them, so the queue's deletes keep their own stamps. The trigger uses only SQL that SQLite 3.36, which stock Zandronum 3.3 ships, parses.
- Outboxes are named from the database's real path (`database_path_compute.cpp`: symlinks followed, Windows short names expanded, one spelling, case kept), so every path a server is given to the same file names the same outboxes; the orphan scan also looks under the path as given, for outboxes an older build named.
- A file that is full (`database_maxpagecount` or the disk) is named on the console once a minute while batches wait on it, and `fua_db_status` shows its pages.
- One file shared over a network drive is not safe in any mode: docs/database-shared-file-risk.md. Backups, moves and restores: docs/database-hosting.md. Known limits and their plans: docs/database-future-work.md.

## Engine hooks

- `za_database.cpp`: the ACS-facing `DATABASE_*` calls go through the store when it is open, else straight to `database_compute.cpp`. `DATABASE_Init` applies the pragmas, binds the statements, starts the checkpointer and opens the store. `DATABASE_EndTic` ends the store's tic. `database_ClearHandle` closes the store (draining up to 3 s; the rest waits in the outbox), then the checkpointer and the statements. `DATABASE_EnableWAL`/`DATABASE_DisableWAL` set `fua_db_wal`.
- `g_game.cpp`: `G_TickerEnd` calls `DATABASE_EndTic`, on clients, with the game thread, and on the dedicated server alike.
- `p_setup.cpp`: `P_SetupLevel` calls `DATABASE_LevelScriptsEnding` before the old level's ACS modules are unloaded.
- `tests/CMakeLists.txt` builds the engine's SQLite into the test binary.

## Testing

- Unit tests beside each `computation/*_compute.cpp`, on real SQLite (in memory and on disk), including random comparisons with the old calls while the writer and the acknowledgements lag, and three servers with skewed clocks on one file.
- `fuactl scenarios dbedge` drives the engine through `tools/fuactl/fixtures/dbedge` (an ACS library, source and BCC build) on prepared database files: reads while the writer lags, null names, 64-bit overflow, transaction misuse, a transaction its level left open, UNLOADING writes, 5000 writes in a tic, clean quit, hard kill and restart, stock-written rows, a crashed server's part-applied outbox, and an orphaned outbox beside a corrupt one.
