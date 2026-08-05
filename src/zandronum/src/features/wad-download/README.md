# features/wad-download

Downloads the files a server wants that the player does not have, over HTTP, from mirrors — so
"you are missing brutal.wad" stops being the end of a join attempt.

## Where the design came from

Odamex (`client/src/cl_download.cpp`, `client/src/otransfer.cpp`). Studied, not copied: the code
here is ours, the shape of the solution is theirs, and it is the right shape.

**The important part is what the game server does: nothing.** It serves no file bytes at all.
Modern Odamex's `SV_WantWad` (`server/src/sv_main.cpp`) reads the request, prints
`Server: Downloading is disabled` and drops the client — the in-band `clc_wantwad` transfer that
used to exist is gone. What replaced it is a list of ordinary HTTP mirrors: `sv_downloadsites`
advertised by the server, `cl_downloadsites` configured by the player, fetched with libcurl.

That is the answer to *"how did they stop downloads lagging the server?"*, and it is a better
answer than any rate limiter: **there is no in-band transfer to rate-limit.** A 40 MB WAD never
touches the game socket, never competes with ticcmds for the send budget, and ten players joining
at once costs the server exactly what none of them joining costs. A bandwidth cap would have left
every download sharing the one socket and the one thread — the server would merely have stuttered
more slowly. Odamex has no download bandwidth cvar because it does not need one.

## Why we need no protocol extension

Zandronum already has the field. `sv_website` goes out over the launcher protocol as `SQF_URL`,
and `browser.h` describes where it lands as *"Website URL of the wad the server is using"* —
which is precisely what Odamex added `sv_downloadsites` for, already deployed on every Zandronum
server in the world.

So the mirror list is **the server's own advertised URL first, then `cl_fua_downloadsites`**. No
new packet, no version negotiation, and a ZandroX client can download from an ordinary Zandronum
server that has never heard of us.

## The legality gate — where we deliberately differ from Odamex

A PWAD is a mod. An IWAD is the game, and for Doom, Doom II, Final Doom, Heretic, Hexen and Strife
the game is something a player buys.

Odamex keeps a **denylist** of commercial files (`common/w_ident.cpp`: `W_IsFilenameCommercialWAD`,
plus an MD5 table). We keep an **allowlist** and nothing else:

> Every IWAD is assumed commercial. One is downloaded only if its name is on a list of IWADs known
> to be freely redistributable. Unknown means no.

A denylist can only describe games that already existed when it was written — the commercial IWAD
released next year is not on it, so it downloads — and keeping it accurate makes us responsible for
tracking every game in the ecosystem forever. Deny-by-default is both safer and far less to
maintain: we never have to know that `doom2.wad` is sold, only that `freedoom2.wad` is not.

Two gates implement it (`computation/iwadallow_compute.h`):

1. **Before fetching** — a file the server declares as its IWAD must be on the allowlist. PWADs
   pass on name alone; mods are the ordinary case.
2. **After the bytes land** — a file whose header says `IWAD` must *also* be on the allowlist,
   whatever it was called and whichever slot asked for it. This is the gate that actually holds. It
   catches `doom2.wad` renamed to `coolmod.wad` and listed as a PWAD, which no name check can, and
   it is why gate 1 needs no list of games to refuse. Odamex reaches the same place by MD5 — exact,
   but only for the hashes they enumerated, so a fresh release or a differently-patched copy walks
   past it. Reading the file's own header needs no table and never goes stale.

The list lives in **`iwadallowlist.txt` at the repo root** — Freedoom (all spellings), Blasphemer,
Chex Quest 1–3 incl. the Vanilla edition, HacX 1.2 and 2.0, Harmony, Action Doom 2, The Adventures
of Square, REKKR, and Mega Man 8-bit Deathmatch (`megagame.wad`). Filenames follow the engine's own
`wadsrc/static/iwadinfo.txt` "Names" block, so every spelling the loader accepts is covered.

Note what is *not* there and how little it takes: REKKR shipped free as `rekkr.wad`, while "Sunken
Land" is the paid Steam edition under its own filename. Leaving that filename off the list is the
entire mechanism — no second list to keep in step.

### Why the allowlist is a repo file and not a setting

`tools/gen-wadlists.cmake` compiles `iwadallowlist.txt` into the binary at build time. Both the
engine and the test build run it, so the shipped gate and the tested gate are the same list.

Adding a free IWAD is therefore a **pull request** — open to anyone, reviewable, with a commit
behind it. What it is *not* is a CVAR, a lump, or a runtime fetch. An earlier draft had
`cl_fua_iwad_allowlist` for exactly the good reason you would expect (free IWADs keep being made;
players should not wait on our release cadence), and it was wrong: a list anyone can append to is
not a gate. The first thing written to it would be `doom2.wad`, in a server operator's setup guide,
in a config a player pastes without reading. A pull request is the right amount of friction for a
claim about someone else's licence.

`waddownloadsites.txt` is generated the same way and *is* overridable by CVAR — nothing in it is a
legal assertion, so a player replacing the mirror list costs them nothing but their own downloads.
It is a separate file because it goes stale on a completely different schedule: mirrors come and go,
licences do not.

## Freedoom as a stand-in

If a server's IWAD is a game you do not own, and Freedoom can replace it, the join loads Freedoom
and says so rather than failing. Freedoom is a from-scratch BSD-licensed replacement for Doom's
data — Zandronum's own wiki puts it as "will allow users to connect to almost any server plausible".

Two things this is careful about:

- **It is a fallback, never a preference.** The server's real IWAD is resolved first, through the
  same `BaseFileSearch` as everything else. Owning `doom2.wad` always means loading `doom2.wad`.
- **It is not a fix for stock maps.** Freedoom's MAP01 is not Doom II's MAP01, so on a server
  running stock levels the substituted client loads different geometry and Zandronum's level
  authentication rejects it. The case it is actually for is the common one — a server running a
  PWAD that replaces every map, where the IWAD supplies only textures, sounds and actors. The engine
  prints which IWAD it substituted, and `cl_fua_iwad_substitute 0` turns it off.

And because Freedoom is on the download allowlist, a player with *neither* the game nor Freedoom
gets Freedoom downloaded and then joins — the one case where "you are missing a commercial IWAD"
can end in a working join instead of a dead end.

The table is `iwadsubstitutes.txt` at the repo root, PR-able like the others.
`tools/gen-wadlists.cmake` **fails the build** if a replacement is not in `iwadallowlist.txt`: a
substitute we are not allowed to download is no use as a stand-in for a game we are not allowed to
download.

## The other thing a server-chosen string can do

Every filename here comes from a remote host. `computation/downloadplan_compute.h` holds the two
functions that exist purely to be attacked — `IsSafeDownloadName` (path traversal, drive letters,
control characters, Windows reserved device stems, trailing dot/space, and an extension outside the
set the engine actually loads) and `UrlEscapeFileName` (so a crafted name cannot restructure the URL
it is pasted into). They are pure functions with tests rather than checks buried in the transfer
loop, because "did we remember to reject `../..`?" should be answerable without running the game.

## Layout

```
<repo root>/iwadallowlist.txt               the downloadable-IWAD allowlist  (PR to add a line)
<repo root>/waddownloadsites.txt            the default mirror list          (PR to add a line)
<repo root>/iwadsubstitutes.txt             Freedoom stand-ins               (PR to add a line)
<repo root>/tools/gen-wadlists.cmake        compiles all three into a header at build time

zx_waddownload.{h,cpp}                      driver: CVARs, CCMDs, worker thread, main-thread Tick
computation/downloadplan_compute.{h,cpp}    mirror URLs, name safety, escaping     (+ _test.cpp)
computation/iwadallow_compute.{h,cpp}       the legality gate                      (+ _test.cpp)
computation/iwadsubstitute_compute.{h,cpp}  what Freedoom can stand in for         (+ _test.cpp)
```

The transfer itself is `features/net/zx_httpfile.{h,cpp}` (+ `zx_httpfile_win.cpp`): WinHTTP on
Windows — the system HTTP stack, so the Windows build gains no third-party dependency — and
dlopen'd libcurl on macOS and Linux, extending the trick `zx_httpsget.cpp` already used.

## Threading

Exactly the `features/updater` pattern, for the same reason. The transfer blocks for minutes, so it
runs on a detached worker, and the worker touches **nothing** the engine considers single-threaded:
no `Printf`, no CVARs, no `FString`, no wad tables. Everything it needs is snapshotted into a `Job`
before it starts; everything it wants to say goes into a mutex-guarded queue that `Tick()` drains on
the main thread. `Printf` off the main thread has already crashed this engine once — see
`features/updater/zx_updater.h`.

## Division of labour with features/wadreload

This feature decides whether a file **may be downloaded** and puts it on disk. `wadreload` decides
whether a file is **loadable** and refuses to restart onto a bad set. A truncated download is caught
there, not here, so exactly one place in the tree answers "can the engine load this".

## Where files go

`cl_fua_download_dir`, or by default a `Downloads/` folder under `M_GetSavegamesPath()` — per-user
and writable on all three platforms, and `progdir` for a portable install. The folder is registered
once in the config's `FileSearch.Directories`, so `BaseFileSearch` finds what we downloaded: this
run, every run after, and anything the player drops in there by hand. That is why the join path
needs no special case for a downloaded file — the retry resolves it exactly like any other WAD.

## CVARs and commands

| Name | Default | What it does |
|---|---|---|
| `cl_fua_download` | `true` | Master switch. |
| `cl_fua_downloadsites` | `waddownloadsites.txt` | Space-separated base URLs, tried after the server's own. |
| `cl_fua_download_dir` | *(empty)* | Override the download folder. |
| `cl_fua_download_maxsize` | `2048` | Per-file ceiling in MB. Bounds what a mirror can write to disk. |
| `cl_fua_iwad_substitute` | `true` | Load Freedoom when the server's IWAD is a game you don't own. |
| `fua_download <file>` | | Fetch one PWAD by hand. |
| `fua_download_stop` | | Abandon the running transfer. |
| `fua_download_status` | | Progress, or where files go when idle. |

## In-engine hooks

- `d_main.cpp` — one line in `D_DoomLoop`: `zx::waddownload::Tick()`, beside the updater's.
- `features/server-browser/zx_joinserver.cpp` — substitutes Freedoom for an IWAD you do not own,
  starts a download when a join is still missing files, and resumes the join when it finishes.
- `features/server-browser/zx_serverbrowsermenu.cpp` — the footer shows the transfer while it runs.
