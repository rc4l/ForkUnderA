# continue

One button on the global header, pinned to the left, that puts the player back where they left off.

Two shapes of session are remembered:

* **Server** — the address we were connected to, plus the password used. Pressing Continue reconnects.
* **Single** — an offline session, snapshotted to one slot. Pressing Continue loads it.

## Where the state lives

A `continue/` folder under the per-user config root, alongside `identity/`:

```
<config root>/continue/session.txt   the record
<config root>/continue/session.zds   the snapshot a Single record points at
```

One folder, named after what it is, because the two files only mean anything together — deleting
this feature's state should be one obvious action rather than knowing which loose files in the
config root belonged to it. The snapshot is one slot, overwritten: this is "where you left off",
not a save history.

Every way of damaging either file resolves to the button not appearing. Verified by deleting each,
truncating the record, replacing either with random bytes, forging a newer format version, zeroing
both, pointing the record at a directory, and replacing the folder itself with a file: the engine
came up in every case and the button was hidden in every case. With both files read-only the quit
path fails to write and exits cleanly, leaving the previous record intact.

## Why the record is not written from a shutdown hook

`i_main.cpp` registers `atexit(call_terms)`, and `I_FatalError` leaves through `exit()`. The `atterm`
chain therefore runs on a crash exactly as it does on a clean quit, and a record written from there
would faithfully save the crash and then offer to put the player back into it.

So the record is written from the **deliberate** quit (`CCMD quit`/`exit`) and from the moment a join
succeeds. A signal crash never reaches `exit()` at all, so it is safe by omission.

## The server probe

A Server record is checked by asking the server, through the browser's own query path:
`BROWSER_AddServerToList` makes the slot and `BROWSER_RecheckServer` sends the query the browser
would have sent. No answer within four seconds is `Gone`; a different PWAD list is `WadsDiffer`.

`BROWSER_GetListIDByAddress` is exported for this. Scanning `BROWSER_GetAddress` instead does not
work and looks like it does: it answers with a cleared dummy for any slot that is not `AS_ACTIVE`,
so a server that has been asked and not yet answered — or that never will — is invisible to that
scan by construction, and the probe silently never settles.

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
| `features/server-browser/browser.{h,cpp}` | exports `BROWSER_AddServerToList` and `BROWSER_GetListIDByAddress` |
| `g_game.{h,cpp}` | exports `G_DoSaveGame`, the synchronous save the quit path needs |
| `g_level.cpp` | `map` from a client brackets its deliberate disconnect with `Continue_NoteChoosingDestination` |
| `w_wad.{h,cpp}` | adds `W_GetLoadedWadPath`, the real path of a file we already have open |
| `features/server-hosting/zx_hosting.cpp` | `HostStart` names loaded files by path, so the child can find them |

## Computation units

* `continuerecord_compute` — the on-disk format, versioned, refusing anything newer.
* `continueshow_compute` — whether the button exists.
* `continuewrite_compute` — whether this shutdown is worth remembering.
* `continuerehost_compute` — whether a remembered server can be started as we are, needs a reload first, or is missing.
