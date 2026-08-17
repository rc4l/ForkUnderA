---
name: reproducing-a-report
description: How to reproduce a fault someone else observed in a live interactive system, without guessing at the state they were in. Use whenever a bug report arrives as a screenshot, a recording or a description of "here, doing this", and the system has (or could have) a way to be queried while it runs.
---

# Reproducing what someone else saw

Applies whenever the bug lives in **state you were not in**: a place in a level, a scroll
position, a selected row, a camera, a document at a particular zoom. The reporter had it and
you do not, and everything between you and a fix depends on getting it back exactly.

The failure mode this exists to prevent is subtle and expensive: you reproduce something
*similar*, fix that, and the reporter comes back saying it still happens. Three rounds of
that is a morning gone, and each round looks like progress.

## 1. The report is a piece of state. Fetch it; do not reconstruct it

The instinct is to read the state out of the artefact you were given — measure coordinates
off a screenshot, count rows in a video, infer a camera from what is in frame. Every one of
those loses precision, and the amount lost is exactly the amount that decides whether you
land on the thing being described or the thing next to it.

Concretely, from this repo: a fault was reported at a spot in a level. The available probe
printed a position rounded to whole units and a direction vector to two decimals. Two
decimals of direction is about half a degree; at three hundred units that is fifteen units
of miss — one step riser over from the one being described. The fault reproduced three times
in three slightly wrong places before anyone noticed that was the problem.

**So: add a way to ask the running system where it is.** Not a log line to be scraped — a
query that returns the state in the same units the *replay* mechanism takes, to enough
precision to round-trip. If replay only accepts integers, that is part of the bug too; widen
it. Precision that is thrown away at the last step was never there.

Two properties make this work, and both are easy to miss:

- **Read-only.** Then it can be run against the instance the reporter is still using. They
  should not have to stop, or read numbers off their own screen, or hand over the session.
  This is the difference between a tool you use and a tool they use.
- **Structured.** A JSON reply extends when the next question arrives — which surface, which
  item, which mode. A printed line has to be re-parsed by every caller, and the parsing is
  where it rots.

## 2. Put the query in the transport, not in the feature

There is always a tempting shortcut: the subsystem you are debugging already has a
diagnostic command, so add the state to *its* output. It works today, and it is wrong.

The state is not the subsystem's business. When the next report is about a different
subsystem you will add it again there, differently, and now there are two answers with two
sets of rounding. Put it where the general-purpose access lives — the RPC layer, the debug
server, the automation API — and let every subsystem's diagnostics stay about their own
subject.

The same argument applies to the client side: make it a first-class command of the tool
everyone already uses, not a shell script beside it. A script that scrapes a log file is a
parser waiting to break, and it will break on the day you most need it.

## 3. Record it, so the third round is free

Once you can fetch state exactly, spend the extra ten minutes to let it be **saved under a
name**, and let the capture/replay path take that name. Then a report becomes a durable
artefact rather than a message in a scrollback:

    tool here --save some-descriptive-name --note "what is wrong, and what right looks like"
    tool capture --spot some-descriptive-name

The note matters as much as the coordinates. Six weeks later the numbers alone will not tell
you what you were meant to be looking at, and a spot whose meaning has been forgotten is a
spot nobody re-runs. Write what is wrong *and* what correct would look like — the second half
is what lets someone else confirm the fix.

This is also how a one-off reproduction becomes a regression test. The expensive part of a
visual or interactive regression test is never the assertion; it is knowing where to stand.

## 4. Reproduce before diagnosing, and confirm the reproduction is the reported one

With the exact state in hand, get the fault on your own screen *first*. It is worth saying
plainly because the pressure runs the other way: you have a theory, the theory is cheap to
try, and trying it feels faster than setting up the repro.

It is not faster. A fix applied to an unreproduced fault cannot be verified, so it ships on
the strength of an argument — and arguments about visual behaviour are wrong at a rate that
will surprise you. Two rounds in this repo were exactly that: a plausible cause, a plausible
fix, a rebuild, and the reporter's next screenshot showing the same thing.

When it does reproduce, check it is *the same* fault and not a cousin. Compare against the
reporter's artefact directly. Same shape, same place, same conditions.

## 4a. Hold the world still before you measure it

A repro is a camera and a piece of state, and anything in the simulation that moves either one
turns two captures seconds apart into two different experiments. In a game that means the
monsters: they shove the player off the recorded position between the A and the B shot, and
they can kill outright, so a comparison pair comes back as a death screen with the fault
nowhere in it. Both happened here before it was worth writing down.

Whatever the equivalent is in the system under test -- other agents, background jobs, a
retrying queue -- disable it in the repro rather than working around it in the reading. It is
usually three commands and it removes the entire class:

    god          # nothing can damage the observer
    notarget     # nothing comes looking
    kill monsters

The general rule: the only thing allowed to differ between two captures is the one variable
being compared. Everything else is held, not averaged over.

## 5. When the report says "it is X", treat X as a symptom name, not a diagnosis

People name what a thing looks like, and the name carries a cause with it. In this repo a
hard-edged black quad was reported as z-fighting twice. It was not: the pass in question
neither tested nor wrote depth, so it had nothing to fight with. Taking the name at face
value would have sent someone into depth precision for an afternoon.

Read the artefact for what it actually shows, not what it was called. Hard straight edges
mean geometry. Regular banding means two surfaces at one depth. A dead horizontal line means
something with a horizontal plane in it. Speckle means noise in a per-pixel input. The
picture is evidence; the label is a hypothesis, and it is the reporter's, not yours.

## 6. Report back in the same terms

Close the loop with the state, not with prose. "Reproduced at the camera you sent, fixed,
verified across a sweep of pitches from 15 to 45 degrees" is checkable. "Should be fixed" is
not, and invites another round.

If part of it is *not* fixed — a limitation of the approach rather than a bug — say which
part and why, in terms of the thing they observed. A reporter who understands the boundary
stops filing the same report; a reporter told "fixed" files it again.
