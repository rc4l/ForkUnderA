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
- Where no engine answers (a browser page, which has no bridge, or an engine that already crashed),
  `queue add "<description>" --no-instance [--map M]` queues it without a spot. Ask them to type
  `currentpos` in the game console if the place matters, and `queue note` what it prints.

`queue list` shows open items (`--all` includes closed). `queue next` prints the oldest open item with
its full spot record.

## Work an item

A report from the browser build reproduces in headless Chrome rather than an engine window:
`node tools/web/stage.mjs build-web <dir> --wads <iwad>,<map>` stages a page with their wads, and
`node tools/web/probe.mjs <dir> --query "iwad=...&file=...&map=..." --run "god; wait 1000; +attack"
[--autoplay]` plays it, listing what the commands printed, the sounds it started (and any started again), the
screen textures it held and its WebGPU errors; `--soak reload` instead reloads the map and prints what kept growing. The probe's Chrome is always muted, and a minimal template map isolates the fault
better than their mod's busy map.

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
   since a fault only one shows is a level-change bug. When it does not show, try `play --no-gl`: a
   fault only `--no-gl` shows is in the GL-free path. Check it is the fault they reported and not a
   cousin, and pin it to a pixel: `look --spot <spot> --pixel x,y` names the surface (and, under a
   floor, its subsector, sectors, textures and linked lights); `png --px`, `png --boxdiff` and
   `png --diffimg` compare captures (`reproducing-a-report`), and `sector-of --pixel x,y --side gl` names the
   sector whose colour a pixel follows (it repaints the map, so only in your own instance). For what moves, or a light a paused
   `shot` can hide, `burst <tag> --side gl|vk --box ...` takes timed captures with the sim running.
3. **Bisect before theorising.** Flip render cvars inside one paused frame (`ab <cvar> a b --spot`),
   switch features off (`fua_dg_*`), and run the old staged binary against your build. Read how GL
   does it, since GL is the spec this backend matches. For a frame-rate item, `abperf` the suspects
   inside one session before changing anything: a clock's name is not what it measures (#42).
4. **Write findings down as you go**: `queue note <id> "<what you ruled in or out>"`. Context gets
   compacted on long items; the queue does not.
5. **Template regression first** (`template-maps`): the smallest generated map with just the one
   ingredient, as `scenarios/<name>.mjs`. It must FAIL on the current binary before you fix anything;
   if it passes on both builds it tests nothing, so redesign it rather than ship it. When the fix keeps
   the old behaviour behind a cvar, `scenarios <name> --side S --cvar k=v` shows the failure instead.
6. **Fix** following `AGENTS.md`: pure logic in `Compute*` helpers with colocated `*_test.cpp`
   (`writing-tests`), thin engine glue, GL hooks listed in the feature README, one-sentence
   `// [rc4l]` comments.
7. **Verify, all of it:** `build --no-stage --tests` (the engine and `zandrox_tests`, run, the user's binary
   left alone), `node --test`, the
   new scenario now passes, `coverage --since <ref>` holds every changed unit at 100% (`writing-tests`), and
   `node src/cli.mjs scenarios --engine F:/ForkUnderA/build-win/Release/forkundera.exe` is green (the suite already runs every Vulkan side standalone, as players do). Re-shoot the item's spot GL against Vulkan and look at it.
8. **Close it:** `queue done <id> --note "<cause>; <fix>; scenario <name>"`.
9. **Hand the build back:** `build` stages it (the exe, its `.pdb`, the pk3s and `catalogue/`),
   relaunch their session (`play --unpark --port 7801` if you parked it, else `fua-side-by-side`), then take the next item.

### When it will not reproduce

Use their exact steps (a map change, a mouse turn, the same window mode) before you give up; a fault
they see only while something moves ("during the crouch") is caught with `steps <tag> --hold +crouch`,
one paused tic at a time. Judder, freezes or "choppy while vid_fps is high" are timing, not pixels: take
`fuactl present --port 7801` while they play before touching the engine (`zandronum-session`), and read
`stats frame/`, `stats phase/` and `hitches` there (`perf-telemetry`). From a browser, ask them to type
`fua_flightdump` and `fua_hitches` in the console where it happens and paste what they print; memory or
"out of memory" reports want `fua_memsnap a`, the action, then `fua_memdiff a`. Then
audit lifetimes instead of re-shooting: a pipeline holding a static binding to a buffer that has been
replaced usually still captures correctly. Fix what the audit finds, close the item with
`queue done <id> --unverified --note "..."`, and tell the user plainly that the fix is unverified.

## Sharing the machine

- **One harness at a time.** Every scenario with a GL side, and every same-frame one, refuses while any
  other engine window is open, the user's included, because a GL frame read back from under another
  window is garbage.
  Before running them, say in one line that you are closing their window and run
  `quit --park --port 7801`, which files their camera, wads and renderer first; afterwards
  `nohup node src/cli.mjs play --unpark --port 7801 --store <their wad folder> &` brings it back
  there, side by side again.
- Reproducing in your locked 7799 instance can run beside theirs.
- Quit engines by token (`quit --port P`) and never by process name, which kills theirs too.
- `shot` cannot capture their side-by-side Vulkan window (it comes back 1x1), which is another
  reason to reproduce in your own instance.

## Reporting

One line when an item is queued. When an item closes, or at the end of a batch, report per item in
their terms: what it was, the cause, the fix, the regression scenario, and anything left unverified.
Commit and push only when they ask, leaving `.vscode/settings.json` out.
