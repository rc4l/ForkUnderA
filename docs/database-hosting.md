# Hosting with an ACS database file

For server hosts whose mods use the ACS database functions (`SetDBEntry`, `GetDBEntry` and the rest) with
`databasefile` pointing at a file. How the engine writes it: `src/zandronum/src/features/database/README.md`.

## The files

| File | What it is | Safe to delete? |
|---|---|---|
| `chars.db` | The database. | No. |
| `chars.db-wal` | Recent commits not yet copied into `chars.db` (WAL mode, the default). Folded back about once a second, and fully on a clean quit. | No, not while it exists: it may hold the newest writes. |
| `chars.db-shm` | The index of the `-wal` file, rebuilt as needed. | Only with every server on the file stopped. |
| `chars.db.fua-outbox-N` (and its `-wal`) | One server's writes that may not be in `chars.db` yet. Empty after a clean quit. | Only when `fua_db_status` (or a clean quit) shows nothing waiting. A non-empty outbox after a crash holds writes nobody else has. |

`N` is a slot, `0` to `63`: each running server holds one, and a starting server takes the first free one.
Outboxes sit beside the real file: through a symlink or a Windows short name (`CHARSD~1.DB`), they sit beside
the file it resolves to, so every path to one file names the same outboxes.

## Backing up

- **Simplest:** stop the server with `quit` (a clean exit, not a kill). The `-wal` is folded in and the
  outboxes are empty, so copying `chars.db` alone is a complete backup.
- **While it runs:** use SQLite's own backup, which is consistent while servers write:
  `sqlite3 chars.db ".backup chars-backup.db"`, or `VACUUM INTO 'chars-backup.db'` from any SQLite tool.
  A plain file copy of `chars.db` while servers run can miss the newest writes (they may still be in the
  `-wal`) or catch a half-copied page. A live backup holds what the file holds: a server's last second or so of
  writes may still be in its outbox, and lands in the next backup.

## Restoring

1. Stop every server that uses the file.
2. Replace `chars.db` with the backup, and delete `chars.db-wal` and `chars.db-shm`, which belong to the file
   being replaced.
3. Decide about the outboxes. Writes made after the backup that never reached the file are still in them, and a
   server that starts will apply them on top of the restored data. Keep them to bring those writes back. Delete
   them to restore the backup exactly as it was.

## Moving or renaming the file

Stop the server with `quit` first, so the `-wal` is folded in and the outboxes are empty. Then move `chars.db`
and point `databasefile` at its new place. After a crash, move the outbox files along with the database, and
keep their names: the outboxes are found by the database's own name.

## After a crash

Keep the outbox files beside the database. The next server to start on the file applies what they hold, the
crashed server's own slot first. Another server that is already running picks them up within a minute.
`fua_db_status` shows what is waiting.

## A full database

`database_maxpagecount` (default 32768 pages; 128 MB with SQLite's default 4 KB pages) caps the file. At the
cap, or with the disk full, the writer cannot apply new batches. It keeps them in the outbox and retries, and the
console names the file, its size and the cap once a minute. Raise `database_maxpagecount` (the engine reopens the
file to apply it), or free disk space. Nothing is lost while it waits.

## Sharing one file between servers

- Several servers on **one machine**, one local file: supported. Each writes through its own outbox.
- A file on a **network drive** (SMB/NFS, a mapped drive, a cloud-synced folder): not supported in any mode,
  see `docs/database-shared-file-risk.md`.
- Players keep one account across servers only when the servers present the same server key (see
  `README.account-auth.md`).
- A stock Zandronum 3.3 server can share the file. Its deletes are marked by a trigger in the file. Its writes
  are not queued, so it can still drop writes that collide.

## Settings

| Cvar / command | Default | What it does |
|---|---|---|
| `databasefile` | `:memory:` | The database file; an in-memory database is not queued and not saved. |
| `fua_db_queue` | `1` | Writes go through this server's outbox and writer thread. `0` writes straight to the file, as stock Zandronum does. |
| `fua_db_wal` | `1` | WAL journal; `0` the rollback journal. `db_enable_wal`/`db_disable_wal` set it. |
| `fua_db_synchronous` | `2` | `2` waits for every commit to reach the disk. `1` can lose the last commits on a power cut. |
| `database_maxpagecount` | `32768` | The file's size cap, in pages. |
| `fua_db_status` | | The outbox, what the file has yet to take, lock retries, the last writer error, and a full file. |
| `dumpdb` | | Prints the whole table. |
