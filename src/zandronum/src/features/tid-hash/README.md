# A wider TID hash

Every actor with a TID sits in one of `AActor::TIDHash`'s chains, picked by the TID's low bits, and every lookup by
TID walks its chain until the TID matches: ACS `GetActorProperty(tid, …)`, `CheckActorInventory`, `Thing_*` and the
rest, through `FActorIterator`. Upstream keeps 128 buckets. A mod that numbers its monsters (DnD gives each its own
TID, from 2 up to 12,801) put ~42 monsters into every chain on Sunder MAP11's 5,400. Any TID many actors share (DnD's
damage-number digits all spawn with one) lengthened the chain of every monster hashing beside it. A loop over every
monster's TID cost milliseconds.

- The table now has 16,384 buckets (`kZxTidHashSize`), so a run of up to 16,384 consecutive TIDs gets a bucket each.
- Lookups are exact: the bucket depends only on the TID, and an actor joins its chain at the head as before, so
  actors sharing a TID keep one chain in the same order and every `FActorIterator` walk returns the same actors in
  the same order. Only unrelated TIDs between them are gone.
- `computation/tidhash_compute.*`: the size and `ComputeTidBucket`.
- Hooks: `actor.h` (`AActor::TIDHash`, `AActor::TIDHASH`, `FActorIterator::Next`), `p_mobj.cpp` (`AActor::TIDHash`,
  `P_IsTIDUsed`).
