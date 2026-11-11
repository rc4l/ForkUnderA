# Known risk: one ACS database file shared by several servers

**Status: open.** No fix is in the tree yet. Read this before changing `za_database.cpp`, before hosting with
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

When two servers write the same file at once, SQLite refuses the second write with `SQLITE_BUSY`. The engine
sets no busy timeout and has no retry, so that write is printed to the console as an error and **dropped**. In a
stress test of two processes writing nonstop, one lost 52 of its 1500 writes and the other all 1500. Real servers
collide far less often, but a collision always loses data.

## Symptoms

- Console lines such as `database is locked` from a `DATABASE_*` function: a write was dropped.
- `database disk image is malformed`, or characters and records that revert or vanish: corruption.

## The fix to consider later

For servers on different machines, one process must own the file and the servers must send it their
writes over the network. The direction being worked on locally already points there:

1. Each server queues its writes, durable in a small private file of its own, and a background thread applies
   them to the shared database in batches, retrying on a locked file instead of dropping the write.
2. Each queued write carries a timestamp and the server's own sequence number. The database applies a set only
   when it is newer than what is stored (deletes leave a marker for this), and records each server's last
   applied sequence, so a resend after a crash is never applied twice.
3. Swapping that background thread's destination from "the SQLite file" to "the owning process" is then the
   cross-machine fix. The game-side half does not change.

What the owning process still needs before it can be built: authentication of the servers that send to it, a
wire format, and a decision on where it runs (one of the game servers, or a separate small service).

Until then: one machine per shared database file, on a local disk.
