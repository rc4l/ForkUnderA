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

Two gates implement it (`computation/iwadallow_compute.h`), and **only the second is a security
boundary**:

1. **Before fetching (name)** — a file the server declares as its IWAD must be on the name
   allowlist. This is an *early-out*, not the gate: it stops us pulling 40 MB of something we would
   only delete, and transiently writing a commercial IWAD to disk is still downloading it. A filename
   is a claim made by the server, so it can never be what we rely on.
2. **After the bytes land (hash)** — a file whose header says `IWAD` is kept only if its **SHA-256**
   is one we shipped. This is the gate. `doom2.wad` served by a mirror under the name
   `freedoom2.wad` passes every name check ever written and fails this one.

The header magic (`IWAD` vs `PWAD`) selects between them. Without it the only rule would be "not in
the hash list → refuse", which would refuse every PWAD ever made: we can enumerate free IWADs, we
cannot enumerate mods.

Odamex arrives at the same place from the other direction — an MD5 **denylist** of commercial files.
That works for them because the set they enumerate stopped growing. It does not work now: `doom2.wad`
alone shipped as 1.666, 1.666g, 1.7, 1.7a, 1.8, 1.9, a French build, BFG Edition and the 2024 KEX
re-release, and Doom-engine games are still being *sold* (Selaco, Beyond Sunset, Hedon). A denylist
has to be complete in both directions — every past version and every future release — and one miss
means the file downloads. An allowlist has to be complete in neither.

It also settles the hash choice. Odamex can use MD5 because a collision against a denylist merely
refuses something harmless; a collision against an *allowlist* is the gate falling open, and
chosen-prefix MD5 collisions are practical. So: **SHA-256**, via OpenSSL, which this build already
links for csrp.

The cost lands on us instead, and we take it deliberately: free IWADs have versions too, so each
release needs a line in `config/iwadallowlist.txt`. When one is missing the failure is loud and safe — the
download is refused, never accepted. Today that means Freedoom and FreeDM (four releases each) are
downloadable and the other allowlisted IWADs are not, because nobody has hashed them from an
authoritative source yet. They are unaffected as IWADs a player already owns.

The list lives in **`config/iwadallowlist.txt`** — Freedoom (all spellings), Blasphemer,
Chex Quest 1–3 incl. the Vanilla edition, HacX 1.2 and 2.0, Harmony, Action Doom 2, The Adventures
of Square, REKKR, and Mega Man 8-bit Deathmatch (`megagame.wad`). Filenames follow the engine's own
`wadsrc/static/iwadinfo.txt` "Names" block, so every spelling the loader accepts is covered.

Note what is *not* there and how little it takes: REKKR shipped free as `rekkr.wad`, while "Sunken
Land" is the paid Steam edition under its own filename. Leaving that filename off the list is the
entire mechanism — no second list to keep in step.

### Why the allowlist is a repo file and not a setting

`tools/gen-wadlists.cmake` compiles `config/iwadallowlist.txt` into the binary at build time. Both the
engine and the test build run it, so the shipped gate and the tested gate are the same list.

Adding a free IWAD is therefore a **pull request** — open to anyone, reviewable, with a commit
behind it. What it is *not* is a CVAR, a lump, or a runtime fetch. An earlier draft had
`cl_fua_iwad_allowlist` for exactly the good reason you would expect (free IWADs keep being made;
players should not wait on our release cadence), and it was wrong: a list anyone can append to is
not a gate. The first thing written to it would be `doom2.wad`, in a server operator's setup guide,
in a config a player pastes without reading. A pull request is the right amount of friction for a
claim about someone else's licence.

`config/waddownloadsites.txt` is generated the same way and *is* overridable by CVAR — nothing in it is a
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

The table is `config/iwadsubstitutes.txt`, PR-able like the others.
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
config/iwadallowlist.txt                    which IWAD names, and which SHA-256 builds  (PR-able)
config/waddownloadsites.txt                 the default mirror list                     (PR-able)
config/iwadsubstitutes.txt                  Freedoom stand-ins                          (PR-able)
config/waddirectories.txt                   where other tools keep WADs                 (PR-able)
tools/gen-wadlists.cmake                    compiles all four into a header at build time

zx_waddownload.{h,cpp}                      driver: CVARs, CCMDs, worker thread, main-thread Tick
computation/downloadplan_compute.{h,cpp}    mirror URLs, name safety, escaping     (+ _test.cpp)
computation/iwadallow_compute.{h,cpp}       the legality gate                      (+ _test.cpp)
computation/iwadsubstitute_compute.{h,cpp}  what Freedoom can stand in for         (+ _test.cpp)
zx_filehash.{h,cpp}                         SHA-256 / MD5 of a file, streamed
zx_wadsearch.{h,cpp}                        IWAD lookup matching the engine's own
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

## Verifying mods, separately from licensing

Zandronum already advertises an MD5 per PWAD over the launcher protocol (`SQF2_PWAD_HASHES`). The
browser used to read those and throw them away; it now stores them, and a downloaded PWAD is checked
against the hash the server itself published. That closes the lying-mirror hole for mods: a site
serving the wrong file — or a stale version — under the right name is rejected.

This is **integrity, not authorization**, and the distinction is the reason the IWAD gate does not
work this way. A hash the server chose cannot gate a file the server requested: a hostile server just
advertises the name `freedoom2.wad` with whatever digest it likes. It proves "I got the file this
server runs", which is worth having and is all it claims. MD5 here is not our choice — it is what the
protocol carries — and it is fine for the job, since forging it means already controlling the server
that told you the hash.

A file that fails verification falls through to the **next mirror** rather than failing the join: a
bad copy on one site should not deny a file the next site has correctly. Only an exhausted list is an
error, and it reports the last rejection so "nobody had it" and "everybody had the wrong thing" are
distinguishable.

## Division of labour with features/wadreload

This feature decides whether a file **may be downloaded** and puts it on disk. `wadreload` decides
whether a file is **loadable** and refuses to restart onto a bad set. A truncated download is caught
there, not here, so exactly one place in the tree answers "can the engine load this".

## Where it looks before downloading

Nothing is fetched until the file is genuinely absent, and "absent" means two different searches
because the engine has two:

| | used by | Windows default |
|---|---|---|
| `FileSearch.Directories` | `BaseFileSearch`, i.e. `-file` PWADs | `$PROGDIR`, `$DOOMWADDIR` |
| `IWADSearch.Directories` | `FIWadManager::IdentifyVersion`, i.e. the IWAD | `.`, `$DOOMWADDIR`, `$HOME`, `$PROGDIR` **+ every Steam library** via `I_GetSteamPath()` |

The join originally resolved *both* the IWAD and the PWADs through `D_AddFile`, so the IWAD lookup
used the **PWAD** path and never saw Steam. A player who bought Doom II on Steam joined a
`doom2.wad` server, we failed to find an IWAD the engine locates at startup without trouble, and
substituted Freedoom — telling someone who owns the game that they don't. `zx_wadsearch.h` is the
fix: after `D_AddFile` misses on an IWAD, walk `IWADSearch.Directories` and the Steam libraries the
same way `IdentifyVersion` does, and only then consider substituting.

`config/waddirectories.txt` adds the folders other Doom tools use — Doomseeker/Wadseeker's
download target (`DataPaths::localDataLocationPath()`, plus its older `.doomseeker` layout) and
GZDoom's. Entries are platform-tagged and filtered at generation time, so only the current platform's
are compiled in; paths keep their `$VAR`/`~` form and go through `NicePath`. Ones that exist are
registered into **both** config sections — unexpanded, once each, visible in the ini where a player
can remove them. A mod already downloaded through Doomseeker should not be downloaded again for
anything, not just for a join.

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
| `cl_fua_downloadsites` | `config/waddownloadsites.txt` | Space-separated base URLs, tried after the server's own. |
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
