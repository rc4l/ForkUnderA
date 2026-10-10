---
name: report-queue
description: Queue problems the user points at in their live ForkUnderA session (position, aim and a description), then work them one by one with the reproduce, template-test, fix, verify loop. Use when the user says "queue this", "add this to the queue", "here's another one", points at a renderer problem while playing, or asks to work, list or clear the queue.
---

# Report queue

Two halves. **Queueing** is quick and happens while the user plays: grab where they are standing and
what they said, then go back to work. **Working** takes the oldest open item through the loop below.
Each item is a saved spot in `tools/fuactl/spots.json` plus an entry in `tools/fuactl/queue.json`
(local, gitignored), and `fuactl queue` is the only thing that edits either.

Run everything from `tools/fuactl` as `node src/cli.mjs …`.

## Queue a report

The moment the user points at something, capture it, before you reply or touch anything:

```bash
node src/cli.mjs queue add "<their words, then what the screenshot shows>" --port 7801
```

- `--port 7801` alone finds their token (`.play-session-7801`). If their session predates that file,
  pass `--token` from wherever you saved it when you launched it.
- The camera is read first and the wad set and renderer cvars after, so it is their vantage even if
  they move a second later. `--name <spot>` overrides the generated `q<id>-<first-words>` spot name.
- If they got there by `nextmap` or `changemap` from another map, `queue note <id>` the route; the
  first fix has to know whether the map was loaded directly.
- Write the description as a symptom and where it is, never a diagnosis: "ceiling over the stairs is
  black on Vulkan, fine on GL; screenshot: top third of the frame". If they attached a screenshot, say
  what it shows, since it does not survive the conversation and the queue does.
- Reply in one line (`#3 queued at q3-ceiling-over-the-stairs`). Then, unless they said to hold, keep
  working the queue; a new item never interrupts the one in progress.

`queue list` shows open items (`--all` includes closed). `queue next` prints the oldest open item with
its full spot record.

## Work an item

1. **Read it.** `queue next`. The spot's `loaded` says which wads the map needs; launching without them
   silently lands in the IWAD's MAP01 (`fua-side-by-side`, `fua-capture-spot`).
2. **Reproduce in your own instance**, never theirs:

   ```bash
   export FUACTL_ENGINE=F:/ForkUnderA/build-win/Release/forkundera.exe FUACTL_PWADS=<folder holding the spot's wads>
   nohup node src/cli.mjs play --spot <spot> --lock --port 7799 --cvar fua_dg_standalone=0 > /tmp/mine.log 2>&1 &
   node src/cli.mjs ready --port 7799
   node src/cli.mjs shot <tag> --spot <spot> --port 7799
   ```

   `play --spot` takes the spot's map and wads (by name, from `FUACTL_PWADS` or `--store`) and stands
   the player there. Reproduce it two ways: loaded directly, and reached the way the user did, with
   `travel --to <MAP>` (`nextmap` until there) from where they started. Note which route shows it,
   since a fault only one shows is a level-change bug. Check it is the fault they reported and not a
   cousin, and pin it to a pixel: `look --spot <spot> --pixel x,y` names the surface (and, under a
   floor, its subsector, sectors, textures and linked lights); `png --px`, `png --boxdiff` and
   `png --diffimg` compare captures (`reproducing-a-report`). For what moves, or a light a paused
   `shot` can hide, `burst <tag> --side gl|vk --box ...` takes timed captures with the sim running.
3. **Bisect before theorising.** Flip render cvars inside one paused frame (`ab <cvar> a b --spot`),
   switch features off (`fua_dg_*`), and run the old staged binary against your build. Read how GL
   does it, since GL is the spec this backend matches.
4. **Write findings down as you go**: `queue note <id> "<what you ruled in or out>"`. Context gets
   compacted on long items; the queue does not.
5. **Template regression first** (`template-maps`): the smallest generated map with just the one
   ingredient, as `scenarios/<name>.mjs`. It must FAIL on the current binary before you fix anything;
   if it passes on both builds it tests nothing, so redesign it rather than ship it.
6. **Fix** following `AGENTS.md`: pure logic in `Compute*` helpers with colocated `*_test.cpp`
   (`writing-tests`), thin engine glue, GL hooks listed in the feature README, one-sentence
   `// [rc4l]` comments.
7. **Verify, all of it:** build the engine and `zandrox_tests`, run the gtests and `node --test`, the
   new scenario now passes, and `node src/cli.mjs scenarios --engine F:/ForkUnderA/build-win/Release/forkundera.exe`
   is green. Re-shoot the item's spot GL against Vulkan and look at it.
8. **Close it:** `queue done <id> --note "<cause>; <fix>; scenario <name>"`.
9. **Hand the build back:** copy `forkundera.exe` and `.pdb` from `build-win/Release` to `dist-windows`,
   relaunch their side-by-side session at the camera you saved (`fua-side-by-side`), then take the next item.

### When it will not reproduce

Use their exact steps (a map change, a mouse turn, the same window mode) before you give up. Then
audit lifetimes instead of re-shooting: a pipeline holding a static binding to a buffer that has been
replaced usually still captures correctly. Fix what the audit finds, close the item with
`queue done <id> --unverified --note "..."`, and tell the user plainly that the fix is unverified.

## Sharing the machine

- **One harness at a time.** `scenarios` and every template comparison refuse while any other engine
  window is open, the user's included, because a GL frame read back from under another window is garbage.
  Before running them, save their camera (`rpc player.camera --port 7801`), say in one line that you are
  closing their window, run `quit --port 7801`, and relaunch them at that camera afterwards.
- Reproducing in your locked 7799 instance can run beside theirs.
- Quit engines by token (`quit --port P`) and never by process name, which kills theirs too.
- `shot` cannot capture their side-by-side Vulkan window (it comes back 1x1), which is another
  reason to reproduce in your own instance.

## Reporting

One line when an item is queued. When an item closes, or at the end of a batch, report per item in
their terms: what it was, the cause, the fix, the regression scenario, and anything left unverified.
Commit and push only when they ask, leaving `.vscode/settings.json` out.
