---
name: divergence-triage
description: How to find a bug when your reimplementation disagrees with a working reference (renderer backend vs GL, new codec vs old, rewritten subsystem vs original). Use whenever the symptom is "ours looks/behaves different from theirs" and the two can be run side by side.
---

# Triaging a divergence from a reference implementation

Applies whenever you have **two implementations of the same thing and one of them is known
good**: a Vulkan backend beside the GL renderer, a rewritten parser beside the original, a
cached path beside the uncached one. The reference is not just a target — it is a *test
oracle you can query*, and most of the work is learning to query it instead of squinting at
the output.

Everything below was paid for. The costs are quoted so you can recognise the shape of the
mistake rather than just the rule.

## 1. The bug lives in the disagreement, so read both sides

One number from your side says nothing. `alpha 1.000` looks perfectly reasonable until the
reference says the same surface has alpha 0.251.

Build a probe that answers **the same question of both implementations at once** and prints
them together. The three answers are different bugs that look identical from outside:

| Reference has | Yours has | Bug class |
|---|---|---|
| a thing | nothing | coverage — it never reached you |
| a thing | the thing, wrong value | classification — you captured the wrong field |
| nothing | a thing | staleness — you kept something it discarded |

Cost of not having this: four rebuild-and-re-check cycles on one surface, each ending in a
guess. The probe found it on first use.

## 2. Instrument the data, not the rendered result

A screenshot cannot distinguish *absent* from *present but culled* from *present with the
wrong value*. Neither can a diff percentage. Print the actual captured values — and print
them **by name, not by presence**.

> A pointer was non-null and pointed at the null texture. The diagnostic printed
> `baseTex yes` for a week. Printing the *name* — empty — exposed it in one run.

Truthy checks are not instrumentation. `!= NULL`, `> 0`, `is set` all hide the case where a
field is populated with a meaningless value.

## 3. Capture the reference's decision; never re-derive it

If the reference computed something, **store what it computed**. Recomputing it from the same
inputs is a second implementation, and it will drift.

Three separate bugs in one session, all this mistake:

- Winding taken from the plane's normal instead of the reference's own "which side is this
  viewed from" flag. The normal is *usually* the same and is wrong for the interesting case.
- A texture re-derived from `sector->GetTexture(planeIndex)` instead of the plane object the
  reference had already resolved. Wrong twice, in two different ways.
- A blend mode computed from an alpha value instead of read from the draw list the reference
  had already sorted the item into.

The tell is a comment that says "this is the same as what X does". If it is the same, use X.

## 4. When a captured value is wrong, look at what writes it *after* you

Before assuming the capture is wrong, check that nothing overwrites it downstream. A shared
"fill in the defaults" helper called after your assignment will silently undo it, and the
capture code reads as correct in review because it *is* correct.

> Wall alpha was assigned three lines before a shared shading capture that resets alpha to 1.
> The equivalent flat path had the opposite ordering and worked. Written a day apart.

Search for every write to the field in the code path, in order. This is faster than reasoning.

## 5. A cache of the reference's output freezes anything the reference recomputes

This is the highest-yield insight here, and it generalises well beyond rendering.

If the reference recomputes something per frame/request/tick, and you cache its output, you
have silently made that thing constant. Caches turn *transient* correctness into *permanent*
wrongness, and the failures are position- and history-dependent, so they look random.

Symptoms from one session:

- Animated textures stopped animating (the cache held a resolved frame).
- Both faces of a two-sided surface were drawn at once (the reference picks one per frame
  based on the viewer; the cache accumulated both, permanently).
- Geometry vanished forever after a switch was pressed (invalidation dropped it, and a
  sticky "never cache this" flag then prevented it ever being rebuilt).

Ask of every cached field: *does the reference recompute this, and on what?* If the answer is
"the viewer's position" or "the clock", it cannot be cached without an invalidation story.

## 6. Count things, not attempts

A counter incremented in a hot path counts *attempts*, and a rejected item is usually retried
every frame. `541,703 rejections` on a map with `1,317` relevant items is not a measurement.

Write the census that walks each item **once** and reports what became of it. It costs twenty
lines and answers "how much is missing and why", which the attempt counter never can.

> The attempt counter pointed at the wrong subsystem entirely. The census showed the suspected
> category was 83.8% complete and the real gaps were elsewhere.

## 7. Validate a diagnostic against a build you know is broken

**A check that has never failed is not yet a check.** Before trusting a new test or probe, run
it against the broken state — stash the fix, rebuild, confirm it fails.

> A frame-to-frame pixel diff "proved" an animation was working: 49% of pixels changed. Then
> the same test against the unfixed build gave 43%. Other things in the frame were animating.
> The test would have passed against the bug it was written for.

When a pixel test cannot isolate the thing, instrument the mechanism instead: count how many
times the code path that *should* fire actually fires. Zero versus non-zero is unambiguous
where a percentage is not.

## 8. Silent-and-total failures belong in unit tests

Note the shape of these bugs: not "slightly wrong output" but *every ceiling in the level
disappears*, *the surface never animates again*, *the texture is completely different*. Total,
silent, and driven by a small decision with a handful of inputs.

That is exactly what a unit test catches and a screenshot does not. Extract the decision into
a pure function, call it from the shipping path (not a copy), and test the cases by the
**symptom they prevent**:

```cpp
TEST(FlatMeshCompute, AMixtureWithinOneGroupIsNotConsistent)   // the 3D floor tops vanishing
TEST(FlatMeshCompute, AdjacentFloorsSharingAnEdgeAreNotAnOverlap)  // the predicate that would flag half the map
```

A failure then names the user-visible fault, not an abstraction. See `writing-tests` for the
mechanics — `Compute*` naming, colocated `*_test.cpp`, the 100% gate.

Include the negative cases. A predicate with no "must NOT fire" tests will be written too
loosely, report everything, and be worth nothing.

## 9. Record the reproduction, not the screenshot

When someone shows you a fault, capture the exact camera/input state as **named data** in the
repo, with what is wrong there and what correct looks like. Then re-checking is one command
forever, and the fix you ship later cannot silently un-fix it.

Two fixed faults were re-broken within the hour by a later change, and only caught because a
human happened to look again.

## Order of operations

1. Reproduce, and **record the reproduction as data**.
2. Query both implementations at the point of disagreement. Do not guess a cause first.
3. Classify: coverage, classification, or staleness (§1).
4. Fix at the source — prefer capturing the reference's own decision (§3).
5. Prove the fix by the mechanism, and prove the *proof* against the broken build (§7).
6. Extract the decision and unit-test it by symptom (§8).
