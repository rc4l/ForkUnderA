# ACS database: known limits and the plans for them

The queued write path (`src/zandronum/src/features/database/README.md`) is crash-proof and safe for several
servers on one machine. These are the limits left, from the most to the least serious, each with what goes wrong,
how likely it is, a plan, how to test it, and a size.

## 1. Servers on different machines sharing one database

**What goes wrong.** SQLite relies on the file system's locks, and network drives do not provide them reliably.
Two machines writing one file over SMB or NFS can corrupt it, in every journal mode
(`docs/database-shared-file-risk.md`).

**Likelihood.** Certain to bite any cluster that tries it. Today the only safe setup is one machine per file.

**Plan: one process owns the file.**

1. **The owner.** A small database service on the machine with the file, either a mode of the dedicated server
   (`-dbhost`) or a separate binary. It runs the writer code that exists today (`ComputeDatabaseApplyBatch`), so
   newest-wins, delete markers and exactly-once replay come with it.
2. **Writes.** Each game server's writer thread sends its batches over TCP instead of applying them, and waits for
   the owner's acknowledgement of the applied batch number. Outboxes stay as they are: a server keeps queuing,
   durably, while the owner is unreachable, and resends on reconnect. `fua_Applied` makes resends harmless.
3. **Reads.** `GetDBEntry` returns a value at once, so a network round trip per read is not possible (500 reads
   at 1 ms is half a second of tic). Each server keeps a **local replica**, a SQLite file of its own that the owner
   streams every applied batch to. Reads stay local and as fast as now. The overlay already shows the server's
   own writes before they come back. Other servers' writes appear after the replication delay.
4. **Resync.** A replica that falls behind or starts empty asks for every batch past its last applied number. The
   owner keeps a window of recent batches, or sends a full snapshot (`VACUUM INTO`) when the gap is past it.
5. **Authentication.** The owner accepts only servers whose server key it trusts (the signed identity in
   `features/identity`), configured as a list of key fingerprints.
6. **Accounts.** Every server of a cluster must present the same `server-account-auth.key`, or a player is a
   different account on each. That holds today too and needs saying in the docs.

**Decisions first.** Where the owner runs (a server mode keeps one binary to ship; a separate service survives a
game server's restart); the wire format (reusing the net layer's framing, or a small protocol of its own); and
whether replicas are full copies or keep only namespaces the server has read.

**Tests.** Unit tests for the protocol's encoding and the resync window, with golden bytes as the net code
has. A local scenario with an owner, two servers and a client on loopback: writes cross servers, an owner restart
loses nothing, a server cut off for a minute resyncs, and an untrusted server is refused. Network faults come from
`fua_net_fakeloss`.

**Size.** Large: four or five PRs (the owner and protocol, replicas, resync, authentication, the cutover with docs).

## 2. A set and an increment of one key from different servers, landing out of order

**What goes wrong.** Within one server every write applies in order. Across servers, sets and deletes resolve by
their stamp, but increments always add, and an increment raises the row's stamp:

- an older set landing after a newer increment is refused, so the set is lost;
- an older increment landing after a newer set is added anyway, so a reset is undone.

**Likelihood.** Needs two servers writing one key with a set and an increment while one of the writers lags,
which only happens under lock contention. A cross-server counter that is also reset is the realistic case. DnD
writes a character from one server at a time, so it is not affected.

**Plan: keep the history a counter needs.**

- **The model.** A key's value is its newest set (or delete) plus the increments stamped after it.
- **A side table** `fua_Increments(Namespace, KeyName, Stamp, Server, Delta)` holds increments newer than the
  key's last set.
- **Applying.** A set records its stamp and recomputes the value from the increments after it. An increment
  older than the key's last set is dropped; a newer one is recorded and added.
- **Compaction** folds increments into the row once every live server's applied stamp has passed them. The
  oldest acknowledged stamp per server is already in `fua_Applied`.
- **Stock servers'** increments write the row directly and stay outside the history. Document that.

**Tests.** The existing three-server random test with sets and increments mixed on the same keys. Its
reference is the stamp-ordered fold of every op, which today's code would fail and the new one must match. Add
compaction under random delivery orders.

**Size.** Medium: one PR, writer and schema only.

## 3. Two servers' writes with exactly the same stamp

**What goes wrong.** The last to land wins. It needs two servers stamping the same 0.1 ms step.

**Likelihood.** Very low; mostly a correctness footnote.

**Plan.** Break ties by server id. Store the stamp as `"<stamp>|<server id>"`: stock engines and `CAST(... AS REAL)`
read the number in front, and the writer compares the id part on a tie.

**Tests.** A unit test with equal stamps from two servers in both landing orders.

**Size.** Small.

## 4. More than 64 servers on one file

**What goes wrong.** The 65th server finds no free slot and writes straight to the file, as stock Zandronum does,
dropping writes that collide. The console says so.

**Plan.** Raise `kDatabaseOutboxSlots` to 256. The orphan scan only opens slot files that exist, so the cost is a
few more failed opens a minute. With item 1 the limit goes away, since only the owner writes the file.

**Tests.** The existing claim tests with the larger count.

**Size.** Tiny.

## 5. A long reader keeping the log from folding back

**What goes wrong.** A tool holding a read open on the file, such as a live dashboard or a stuck stock server
transaction, stops checkpoints at its snapshot, so `chars.db-wal` grows until it lets go. No data is lost.

**Plan.**

- The checkpointer already reports how many log frames it copied. When it copies none for a few minutes while
  the log grows, the console says a connection is holding a read open.
- `fua_db_status` shows the log's size.

**Tests.** A checkpointer test with a reader holding a snapshot: the warning appears and goes once the reader
closes.

**Size.** Small.

## 6. Transactions across a map change

**What goes wrong.** A level change now ends any mod transaction its scripts left open, keeping its writes. A mod
that ended a transaction on purpose in the next map gets "no transaction is active" there. Its writes are already
saved, so only the message differs.

**Plan.** If a mod needs the old behaviour, add `fua_db_keep_transactions_across_maps` (default off). The
one-minute limit still applies.

**Tests.** The `dbedge` scenario with the cvar on: the transaction survives the map change and ends where the
mod ends it.

**Size.** Small, and only if a mod asks.

## 7. A backup command

**What goes wrong.** Hosts must stop the server or use the `sqlite3` tool to back up safely
(`docs/database-hosting.md`).

**Plan.** `fua_db_backup <file>`, rcon-safe, running SQLite's online backup on a thread of its own. It writes a
consistent copy, then a line saying where it went and how many batches were still pending. Optionally it also
copies the outbox, for a backup that includes those batches.

**Tests.** A unit test of the backup while the writer applies batches, and a `dbedge` case reading the backup.

**Size.** Small.

## 8. Database identity in each outbox

**What goes wrong.** An outbox is found only by its name. Hard links, and two databases restored under each
other's names, could pair an outbox with the wrong file and replay its writes there.

**Likelihood.** Very low, and only by hand.

**Plan.**

- Give each database a random id in a `fua_Meta` table.
- Record the id in an outbox when a server claims it.
- Replay an outbox only into the database whose id it records. Older outboxes without an id are accepted, as now.

**Tests.** An outbox recording another database's id is named on the console and left untouched.

**Size.** Small.

## Order

1. Items 4 and 5: small, and they remove silent failure modes.
2. Item 2, before any mod relies on cross-server counters.
3. Item 1, once its design questions are answered.
4. Items 3, 6, 7 and 8 as they are asked for.
