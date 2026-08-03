---
name: fork-exposure
description: How to port an upstream change that is harmless upstream but dangerous here, because something outside the engine reads the value we just changed the meaning of. Covers finding those readers before porting, deciding whether to bump a version, and the rule that a newly discovered hazard ships with a machine check or does not ship. Use for ANY port that changes a stored, transmitted, or externally-parsed representation — enum numbering, field layout, flag bit assignments, lump syntax.
---

# Porting changes that are safe upstream and dangerous here

Upstream is a single program. We are a program plus a savegame format, a demo format, a
launcher query protocol, and a pile of mod-facing constants. So an upstream commit can
renumber something with no consequence at all for them and break three things here — and
their diff will contain no hint that it did, because there was nothing to hint at.

This has happened repeatedly and always the same way: the port looks mechanical, it builds,
it runs, the tests pass, and the damage is to data that is not read until later.

## The question to ask, before porting

> **When something reads this value back, will that reader have been rebuilt at the same
> time we were?**

If yes, changing the representation is free. If no, it is a trap.

Worked through, using the cases that produced this skill:

| Who reads it | Rebuilt with us? | Verdict |
|---|---|---|
| A connected game client | **Yes** — `NETGAMEVERSION` is revision-derived, so mismatched builds already refuse each other | Safe |
| A savegame written last week | No — the file is fixed, our code moved | **Dangerous** |
| A demo recorded last month | No — same | **Dangerous** |
| A launcher / server browser | No — someone else's program, someone else's release schedule | **Dangerous** |
| A mod's DECORATE or ACS | No — the wad is shipped, we are not rebuilding it | **Dangerous** |

Note the shape of that table: the *network* is the safe one and the *files* are the
dangerous ones, which is the opposite of the instinct. Do not reason from "this crosses the
network so it must be the risky one."

Do not memorise the table. It is an illustration of the question, and the question is what
transfers to a surface nobody has met yet.

## Finding the readers — derive, never assume

The reason these slip through is that the upstream diff shows only the definition changing,
never the consumers. Find them yourself, from the value's own name:

1. **Serialized?** `git grep 'arc <<' -- <file>` around the type, and any `Serialize`
   method on the owning class. A value written to an archive is in every savegame ever made.
2. **Transmitted?** `git grep '<TypeName>' src/zandronum/src/sv_commands.h` — anything
   appearing as a parameter there goes out as bytes. Also `NETWORK_Write*` call sites.
3. **Published to outside programs?** `git grep -l 'WriteLong\|WriteByte' src/zandronum/src/features/federated-server-registry/` — whatever answers launcher and
   master-server queries. Third-party software parses what it writes. Find that code by what
   it does, not by a filename: this lived in sv_master.cpp until it was moved into a feature
   directory, and the path will move again.
4. **Mod-facing?** Is the value a DECORATE flag, an ACS constant, an `APROP_`, a line
   special number, a lump keyword? Then shipped wads depend on it.
5. **Config-facing?** Anything archived to the ini and read back on next launch.

If a grep finds nothing, say so in the ledger note — "checked serialization, sv_commands and
the server registry; no reader outside the process" is a derived result and re-checkable. "Looked
fine" is not.

## What to do when you find one — do not stop

**Port it anyway, and keep going.** Upstream's change is upstream's change; being truthful to
it is the whole point of the effort. The exposure is ours to absorb, not a reason to
diverge, and not a reason to stop and ask.

The sequence is:

1. Port the upstream change faithfully.
2. Decide the version bump (below).
3. Make sure a machine check covers the hazard (below).
4. Record the exposure in the ledger row — what reads it, what you bumped, why.
5. Say it plainly in the summary and carry on.

**Do not stop for external consumers.** Launchers, server browsers and anyone else parsing
our output are not a reason to hold a port. Note what changed and move on; that gets sorted
out on our schedule, not mid-backport.

The one conservatism worth inheriting is upstream's own, and it is not politeness. They bump
`SAVEVER` freely and periodically raise `MINSAVEVER` to discard old saves outright — 3100 in
2014, 4556 at HEAD — so saves and demos are cheap to them and to us. But they do **not**
renumber mod-facing constants, because a shipped wad keeps running against the new engine and
a renumbered line special silently changes what that wad DOES. That is a correctness problem,
not a courtesy.

`df0d3543a` shows both halves in one commit: it *added* special 104 rather than repurposing
one, and its own message notes "this change only affects the XLAT mapping, the Hexen format
types behave as before." Follow that instinct — where upstream adds rather than renumbers,
add rather than renumber, for the reason they did.

## Do what upstream did

If upstream bumped a version, bump ours. If upstream did not, do not. Their judgement on
their own change is the default and it does not get relitigated per port.

Where upstream *added* rather than renumbered -- a new special, a new flag bit -- add rather
than renumber, for the reason they did: shipped wads keep running and a renumbered constant
silently changes what they do.

## A newly discovered hazard ships with a check, or it does not ship

If you find a class of exposure that nothing verifies, **build the check first, prove it
fires on the very change that revealed it, then land both together.** Not afterwards, not as
a follow-up.

The reason is specific: these hazards are invisible to every existing gate by construction —
that is why they reached you. A hazard you found by reading is a hazard the next person will
not find by reading.

Properties a good check here has, drawn from the ones that work:

- **Derives its own scope.** The wire-enum snapshot finds which enums to watch by scanning
  `SERVERCOMMANDS_*` parameters, so a new command taking a new enum is covered with no list
  to maintain. A hand-maintained list of things to watch is a list that goes stale.
- **Reads only our tree**, so it runs in normal CI on every PR with no extra checkout.
- **Exact, not heuristic.** Integers and layouts compare exactly; that means no advisory
  period, no triage backlog, and a failure that always means something.
- **Fails with the damage printed**, and says what to do — which version to bump, which
  command regenerates the golden.
- **Has a deliberate accept path** (`--write` a committed golden), so an intentional change
  shows up as a reviewable diff instead of a silenced alarm.

`tools/wire-enum-snapshot.py` is the reference implementation; `tools/protocol-snapshot.py`
is the older sibling it complements.

## Relationship to the other skills

`netcode-adaptation` covers *behaviour* that must be server-authoritative and broadcast.
This skill covers *representation* — the numbering and layout of values, independent of who
computes them. A change can need both: the ceiling port needed no gating at all (sector
movers were already authoritative) but did need two version bumps and a new check.

`sequential-backport` supplies the rule this one specialises: relevance is derived against
the tree as it is now, never declared. Same here — exposure is derived by grepping for the
readers, never assumed from a list.
