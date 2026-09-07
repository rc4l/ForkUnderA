# continue

One button on the global header, pinned to the left, that puts the player back where they left off.

It remembers a **history** rather than a single session: the last several distinct things the player
has been doing, newest first. Pressing the pill with more than one of them opens a list to choose
from; with exactly one it goes straight there, which is what the feature did before the list existed.

Three shapes of session live in that list:

* **Server** — the address we were connected to, plus the password used. Continuing reconnects.
* **Single** — an offline session, snapshotted to a slot of its own. Continuing loads it.
* **Hosted** — a game we hosted. The world lived in the child process and went with it, so what is
  kept is the config that made it: a fresh match on the same terms, not the match we left.

## Where the state lives

A `continue/` folder under the per-user config root, alongside `identity/`:

```
<config root>/continue/history.txt        the list
<config root>/continue/offline-<n>.zds    the snapshot one Single entry points at
```

One folder, named after what it is, because the files only mean anything together — deleting this
feature's state should be one obvious action rather than knowing which loose files in the config root
belonged to it.

**One snapshot per entry**, numbered by the entry's stamp. A single shared slot was right while there
was a single offline record; a history of them would otherwise be ten rows pointing at one save, nine
of them lying about which map they lead to. When an entry falls off the end its snapshot is deleted
with it — worked out from the difference between the old list and the new one, never by scanning the
folder for files nothing points at (that scan would also find the snapshot another copy of the engine
is holding).

Every way of damaging any of it resolves to the button not appearing, or to one row missing. The
history parses entry by entry: a mangled entry costs one row rather than throwing away the other
forty-nine.

## Why the record is not written from a shutdown hook

`i_main.cpp` registers `atexit(call_terms)`, and `I_FatalError` leaves through `exit()`. The `atterm`
chain therefore runs on a crash exactly as it does on a clean quit, and a record written from there
would faithfully save the crash and then offer to put the player back into it.

So the record is written from the **deliberate** quit (`CCMD quit`/`exit`) and from the moment a join
succeeds. A signal crash never reaches `exit()` at all, so it is safe by omission.

## What makes two sessions the same session

Each entry has an identity, and a new session whose identity is already in the list **replaces** that
row and moves it to the top. Three evenings on the same server is one thing done three times, and a
history that showed it three times would have spent three of its rows saying the same sentence.

* A server is its **address** — one that renames itself is still where they go in the evening.
* A local game is its **map and the files it was played with** — MAP01 of one megawad is not MAP01 of
  another, and a history that thought otherwise would overwrite one with the other.
* A hosted game is **what would start it again**: the map, the files and the mode.

Files are compared by name, never by the path they happened to be written down under. A remembered
host config holds `doom2.wad` because that is what the player picked; the config a *running* server
reports holds the absolute path the engine resolved it to. Compared as strings those are two games,
which is how rehosting a row added a second copy of it and left the pill offering to take the player
back to the game they were already inside.

## Order comes from the counter, the column comes from the clock

`stamp` is monotonic and ours; `playedAt` is the system clock and is not. Sorting by the clock would
let a machine whose time is wrong — or which corrects itself while the engine runs — reshuffle a list
the player has learned the shape of. Sorting by the counter cannot, and the clock is still the only
thing that can say "yesterday", so both are kept and each does the one job it can be trusted with. An
entry with no clock (written before the field existed) shows a dash rather than 1970.

## How many it keeps

`cl_fua_continue_history`, 1 to 50, default 10, under **FUA Options → Continue History**. The cap is
applied on the way in AND on the way out, so lowering it trims at the next launch rather than waiting
for the next thing the player happens to play. The floor is one, not zero: zero entries is the feature
switched off, and a size control that switches something off at one end of its travel is two settings
wearing one hat.

## The server probe

A Server record is checked by asking the server, through the browser's own query path:
`BROWSER_AddServerToList` makes the slot and `BROWSER_RecheckServer` sends the query the browser
would have sent. No answer within four seconds is `Gone`; a different PWAD list is `WadsDiffer`.

`BROWSER_GetListIDByAddress` is exported for this. Scanning `BROWSER_GetAddress` instead does not
work and looks like it does: it answers with a cleared dummy for any slot that is not `AS_ACTIVE`,
so a server that has been asked and not yet answered — or that never will — is invisible to that
scan by construction, and the probe silently never settles.

**One question in flight, and one per address ever.** The list can hold several servers, and asking
all of them the moment a menu opens is a query storm aimed at other people's machines on behalf of
rows nobody may click. The row one press would act on is asked without being clicked, exactly as the
single record was; the rest are asked when the player selects them.

## Why a pending server probe still shows the button

Hiding until a probe answers makes the button appear a second after the menu, underneath the
player's cursor, which is how a misclick becomes a reconnect. Showing until a probe says otherwise
costs at worst one press that lands back in the browser with a reason — the path a failed join
already takes. Same asymmetry `headerreach_compute` settles for "Play Online!".

In the list this goes one step further: a row whose probe has come back dead is **dimmed and
labelled**, not removed. Rows that vanish from under a pointer are how a click lands on something the
player did not read.

That makes two different questions, and the code asks them separately. *Usable* is structural — the
snapshot is there and this build can load it — and decides which rows the list shows. *Offerable* is
usable **and** not a server we know is dead, and decides what the pill will do without asking. A dead
server therefore stays visible and pressable in the list, but never becomes the thing a single press
lands on.

## Migrating from the two records

The first launch after this build reads the old `offline.txt` and `server.txt`, inserts them oldest
first so their existing stamps put them in the order the player lived them, writes `history.txt`, and
deletes the two. Their clocks are left at zero rather than set to now: those sessions happened at some
point that nothing wrote down, and stamping them with the moment of the upgrade would have the list
claim the player was in all of them a second ago.

Migration only ever runs when there is **no** history file. An emptied history is written as a file
with no entries rather than deleted, so a player who clears it does not find it back next launch.

## What leaving means

Pressing the pill inside a session is Disconnect, and it goes to **what you left in this process to
get here** — or the main menu if that was nothing. Not "the newest local entry", which is what it was
while there were two records: with a history that answer is some match from last week, so leaving a
rehosted game started an unrelated one, and leaving a game you had picked from the list started the
very game you were standing in.

Leaving with nowhere to go lands on the **title screen with the main menu open**.
`CLIENT_QuitNetworkGame` ends in `ga_fullconsole` and `D_StartTitle` alone begins the attract loop, so
the two obvious versions of this leave the player at a console or in a slideshow while the pill has
just promised them a menu. Both are performed from the tick, because a gameaction issued inside a
teardown is replaced by the teardown's own.

## The list

`DFUAContinueMenu` (`zx_continuemenu.cpp`) — a card in the same visual language as the browser and
the updater's notice, drawn from the same tested geometry. Two columns and no more: what it was, and
when. Keyboard is Up/Down (wrapping), PageUp/PageDown (clamping — a page key that wrapped would make
holding it a loop through the whole list), Home/End, Enter to continue, Del to forget a row, Esc to
leave. The wheel moves the view and leaves the selection alone.

The scrolling is not new: `ComputeRowWindow`, `ComputeRestoredScroll`, `ComputeThumbHeight`,
`ComputeThumbTop` and `ComputeClampedSelection` are the server browser's, already unit-tested. What is
genuinely new is Home and End, which nothing else in this engine implements.

## In-place engine edits

| File | Edit |
|---|---|
| `src/CMakeLists.txt` | registers `features/continue/zx_continue.cpp` and `zx_continuemenu.cpp` |
| `wadsrc/static/menudef.txt` | the `FUAContinueOptions` submenu and its row in `FUAOptions` |
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

## Console

* `fua_continue` — press the pill. With a row number, act on that row instead.
* `fua_continue_list` — print the list, so an E2E can assert on rows rather than pixels.

## Computation units

* `continuerecord_compute` — one record's on-disk format, versioned, refusing anything newer.
* `continuehistory_compute` — the list: identity, dedupe, ordering, the cap, and the file it lives in.
* `continuelist_compute` — where the keyboard cursor goes, including Home and End.
* `continueshow_compute` — whether an entry is worth offering.
* `continuebutton_compute` — what the pill says, where it goes, and whether it asks.
* `continuewrite_compute` — whether this shutdown is worth remembering.
* `continuedepart_compute` — whether leaving a server means "take me back".
* `continuereturn_compute` — when an owed return may actually be performed.
* `continuerehost_compute` — whether a remembered server can be started as we are, needs a reload first, or is missing.
