# continue

One button on the global header, pinned to the left, that puts the player back where they left off.

Two shapes of session are remembered:

* **Server** — the address we were connected to, plus the password used. Pressing Continue reconnects.
* **Single** — an offline session, snapshotted to one slot. Pressing Continue loads it.

## Why the record is not written from a shutdown hook

`i_main.cpp` registers `atexit(call_terms)`, and `I_FatalError` leaves through `exit()`. The `atterm`
chain therefore runs on a crash exactly as it does on a clean quit, and a record written from there
would faithfully save the crash and then offer to put the player back into it.

So the record is written from the **deliberate** quit (`CCMD quit`/`exit`) and from the moment a join
succeeds. A signal crash never reaches `exit()` at all, so it is safe by omission.

## Why a pending server probe still shows the button

Hiding until a probe answers makes the button appear a second after the menu, underneath the
player's cursor, which is how a misclick becomes a reconnect. Showing until a probe says otherwise
costs at worst one press that lands back in the browser with a reason — the path a failed join
already takes. Same asymmetry `headerreach_compute` settles for "Play Online!".

## In-place engine edits

| File | Edit |
|---|---|
| `src/CMakeLists.txt` | registers `features/continue/zx_continue.cpp` |
| `c_cmds.cpp` | `quit`/`exit` record the session before exiting |
| `features/server-browser/zx_joinserver.cpp` | `NoteJoinSucceeded` records the server |
| `features/global-header/zx_globalheader.cpp` | the Continue tab: label, count, pinned index, activation |
| `features/global-header/computation/globalheader_compute.{h,cpp}` | `pinnedIndex` layout and `StepHeaderTabPinned` |
| `mcp_rpc.cpp` | `ui.continue`, for `fuactl continue` |

## Computation units

* `continuerecord_compute` — the on-disk format, versioned, refusing anything newer.
* `continueshow_compute` — whether the button exists.
* `continuewrite_compute` — whether this shutdown is worth remembering.
