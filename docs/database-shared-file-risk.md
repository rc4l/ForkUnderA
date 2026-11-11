# Known risk: one ACS database file shared by several servers

**Status: open for servers on different machines.** Several servers on one machine are safe since PR #407. Read this before changing `za_database.cpp`, before hosting with
`databasefile`, or before telling a server owner how to share character data between servers.

## The risk

Mods keep characters, stats and leaderboards in the SQLite file named by `databasefile`, through the ACS
database functions (`SetDBEntry`, `GetDBEntry`, `IncrementDBEntry`, `SortDBEntries`, …). Hosts that want
players to keep one character across several servers point all of those servers at the same file.

**That file must never sit on a network drive (SMB/CIFS share, NFS mount, mapped drive, cloud-synced
folder).** SQLite relies on the file locks of the filesystem it runs on, and network filesystems do not
provide them reliably. Two servers can then write at the same moment and corrupt the file, in every journal
mode. WAL mode (`db_enable_wal`) is worse there: it also needs shared memory between the processes, which a
network drive cannot provide. This is SQLite's documented limit ("SQLite Over a Network, Caveats and
Considerations" on sqlite.org), not an engine bug.

| Setup | Safe? |
|---|---|
| One server, local file | yes |
| Several servers on the same machine, one local file | yes for the file; see lost writes below |
| Servers on different machines, file on a network drive | **no**: corruption risk |
| A cloud-synced folder (Dropbox, OneDrive, …) | **no** |

## Lost writes, even on one machine

Since PR #407 (`fua_db_queue`, on by default), a write that meets another server's lock waits in this server's
outbox and is retried until it lands, so collisions no longer lose writes. Writes still bypass the queue, and drop
on a collision as stock Zandronum's do, in these cases:

- `fua_db_queue 0`;
- a 65th server on one file, which finds no free outbox slot;
- a stock Zandronum server sharing the file.

In a stress test of two old-engine processes writing nonstop, one lost 52 of its 1500 writes and the other all 1500.

## Symptoms

- Console lines such as `database is locked` from a `DATABASE_*` function: a write was dropped, on one of the
  direct paths above.
- `database disk image is malformed`, or characters and records that revert or vanish: corruption.

## The fix to consider later

For servers on different machines, one process must own the file and the servers must send it their writes over
the network. The queue made that possible: each server's writes are durable, numbered and stamped batches, and the
writer thread that applies them can send them to an owning process instead, with the game-side half unchanged.
The design, what it still needs (local replicas for reads, resync, authentication, where the owner runs) and how
to test it are in `docs/database-future-work.md`.

Until then: one machine per shared database file, on a local disk.

Backing up, moving and restoring a shared file: `docs/database-hosting.md`. The plan for the owning process, with the
other known limits: `docs/database-future-work.md`.
