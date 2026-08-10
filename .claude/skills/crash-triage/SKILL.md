---
name: crash-triage
description: How to triage, fix, and close a native crash filed as a GitHub issue (auto-filed by crash-sync from GlitchTip) in ZandroX. Use whenever you're handed a symbolicated "[crash] ..." issue and need to explain it, find/port a fix, validate by reproducing, and write the Who/What/Where/Why. Also use when investigating any hard crash (EXC_BAD_ACCESS/SIGSEGV) from a stack trace.
---

# Triaging a native crash

Crashes arrive as GitHub issues titled `[crash] <site> (<file:line>)`, auto-filed by
`tools/crash_sync.py` from GlitchTip. Each body has the symbolicated stack **crash-site-first**
(frame 0 = where it died) and is tagged with the exact release commit. Your job: explain it, fix it
(usually by porting the guard upstream already has), validate by reproducing, write a
Who/What/Where/Why on the issue, and close it.

## 1. Read the stack twice
- **Top-down for the fault:** frame 0 is the faulting instruction. Open the source at its `file:line`
  and identify the exact pointer/field being read or written.
- **Bottom-up for the trigger:** the *lower* frames are the calling context (a thinker, a floor
  mover, a map change, a net packet). The bug is almost always "a legitimate path reached this frame
  with an argument that's invalid **right now**."
- **Confirm with lldb** when you can reproduce: `lldb --batch -o run -o "bt" -o "register read" -- <bin> <args>`.
  `EXC_BAD_ACCESS (code=1, address=0x…)` + the faulting register tells you which pointer is bad and
  the offset tells you which field (e.g. `[x1, #0x143]` = the field at that struct offset off a null base).

## 2. Trace the bad value to its origin
Walk the bad pointer/value backward through the frames until you find the line that set it wrong —
often an unchecked reassignment or return a frame or two above the crash. **That line is the real
defect**, even though the fault happens later. (For the canonical example, `AInventory::Touch` does
`toucher = toucher->player->mo` with no null check; the crash surfaces later in `CallTryPickup`.)

## 3. Check whether upstream already fixed it — before writing anything
ZandroX is an old Zandronum base, so the fix usually already exists in a tree you port from. Grep the
**same function** in the local reference trees and diff it against ours:
- `~/repos/Q-Zandronum` — a Zandronum fork; frequently has the exact one-line guard in the same file.
- `~/repos/UZDoom` — modern GZDoom; the logic may have moved to ZScript, but the C++ still shows the
  guard *pattern* (e.g. `player->mo != nullptr` checks scattered through `p_saveg.cpp`, `g_level.cpp`).
The added guard/null-check in their version **is** the fix. Port it per the `upstream-port` and
`provenance-links` skills; tag it `[rc4l]` with a one-line why and the source it matches.

**If the fix does more than early-return on a bad pointer** — if it changes synced gameplay state
(actor position/state/flags, spawn/destroy, health, RNG) rather than just guarding a null — route it
through the `netcode-adaptation` skill: a guard that silently runs on clients too can trade a crash for
a desync. A pure null-check that prevents the dereference is exempt; anything that alters the outcome is
not. (The lower frames in step 1 flag this: a crash reached via a net packet / thinker on the server is
exactly where a naive guard desyncs.)

## 4. Validate by REPRODUCING, not by a unit test
A crash fix is proven by "it no longer crashes doing the thing that crashed it." The state is
engine-wide (actors, sectors, players) and not extractable to a pure `Compute*` helper, so don't force
one — reproduce instead:
1. Apply the minimal guard. `cmake --build build --target zdoom -j4`, then refresh the `.app` bundle
   binary (`cp build/zandronum build/ForkUnderA.app/Contents/MacOS/zandronum`).
2. Drive the exact scenario that crashed via the MCP bridge (see `zandronum-driver`) — same iwad, map,
   bots, sequence.
3. Before the fix it died within seconds; after, confirm it **survives well past that point** (watch
   `pgrep` for ~20s past the trigger).

## 5. Write Who/What/Where/Why on the issue, then close
Post one comment, four short paragraphs, **~3 sentences each**:
- **Who** — who hits it and under exactly what conditions (which actors / modes / maps / timing); and
  who is spared, and why.
- **What** — what the crash is mechanically (fault type, which dereference), and how reliably it fires.
- **Where** — the crash-site `file:line`, the frame that *introduced* the bad value, and the call path
  that reaches it (name the functions, not just addresses).
- **Why** — the root cause: the state that makes the value bad, why that state arises, and why this
  particular trigger (map/mode/timing) lines it up.

Then close the issue with `gh issue close <n> --reason completed`, referencing the fix PR/commit. Keep
the fix and any skill/doc changes on their own branch; ZandroX auto-merges PRs once CI is green.
```
