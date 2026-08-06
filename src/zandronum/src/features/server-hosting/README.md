# server-hosting

Running a server from inside the game, so that hosting no longer means leaving it.

Two clicks: **HOST**, then **START SERVER**. The player ends up playing on their own server, as its
administrator, with no launcher, no second window, and nothing typed outside the game.

## What this is not

It is not a listen server. One ZandroX process cannot be both authority and client: the simulation
is single-instance (`FLevelLocals level`, `players[MAXPLAYERS+1]`, `consoleplayer`) and the role is a
process-wide singleton consulted about 1700 times. Instancing all of that is a rewrite of the
engine's ownership model, and it buys nothing this does not.

So this **owns a child process**. Zandronum already ships a server, and on Windows it is the same
binary the player is running — `-host` is a runtime argument, not a build. Nothing extra ships.

## The orphan guarantee

The one promise that matters: **when the game ends, by any route, the server ends too.** A server
still holding a port after the game is gone is a failure a player cannot diagnose and will not
forgive.

| Platform | Mechanism |
|---|---|
| Windows | Job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Enforced by the kernel whatever kills us, including a task-manager kill. The child is created suspended and assigned to the job *before* it can run, so it cannot spawn anything that escapes a job it was never in. |
| Linux | `prctl( PR_SET_PDEATHSIG, SIGKILL )`, armed between fork and exec. |
| macOS | Neither exists, so the child watches the parent itself — `zx_hostwatchdog.cpp`. |

The watchdog runs on **every** platform, not just macOS: it costs one sleeping thread, and the
alternative is trusting a single lock on the one door that must not be left open.

## Headless on Windows

`-host -fua_hidden` runs `D_DoomMain` directly, with no dialog and no window at all. The ten
`SERVERCONSOLE_*` entry points return early when there is no dialog, so the rest of the server
neither knows nor cares.

The first attempt created the dialog and hid it, which does keep it out of the taskbar but leaves a
real window, a real message pump, and a list view being updated for nobody. A window that exists
only to be hidden is one somebody eventually shows by accident.

This is useful on its own: it is a genuinely headless Windows server, for anyone running one under a
service manager.

## The pipe is ours, not `-stdout`

`-stdout` looks like exactly what a parent reading a pipe would want, and is the opposite. It picks
where to write by probing the handle with `GetFileInformationByHandle`, **which fails on an anonymous
pipe**, and its fallback is `AllocConsole` — a console window on the desktop of a player who asked
for headless.

The child writes to its inherited handle directly (`HostChildEcho`). That hook had to go in the
`-host` branch of `C_Printf`, which returns before `I_PrintStr` and left a hosted server completely
silent — the state in which its owner most needs to hear from it.

Two markers travel up that pipe, both ours so they can only break deliberately:

- `[fua-host] ready` — the server tick has started, so the socket is bound and the map is up.
- `[fua-host] reachable` — see below.

## Reachability is observed, never predicted

A machine cannot tell from the inside whether its port is forwarded. Checking your own port proves
you can talk to yourself; a router agreeing to a UPnP request proves a router replied.

The registry already runs the only test that counts: when a server announces, the registry sends it
an **unsolicited packet from outside** and will not list it until that packet is answered. Arriving
at `SERVER_SERVERREGISTRY_HandleVerificationRequest` means a stranger on the internet reached this
socket — so the answer comes free with the listing the player already wanted.

The panel reports what is known and no more: *waiting*, *reachable*, or *nothing has reached this
server from outside*. Not "your port is closed" — a registry that is briefly down looks identical
from in here, and blaming the player's router would send them to configure something that was never
wrong.

## Generations, not "am I hosting"

Stopping a server disconnects the client, but the client notices **seconds** later — by which time
the player may have started another one. The addresses are identical, so nothing but a counter can
tell the two apart. Asking the loose question tore the new server down; `HostGeneration()` is what
makes teardown mean "am I leaving *this* host".

Found by doing exactly that: stop, start, and watch the second server die to the first's goodbye.

## What the server runs

Whatever **this client** is running. A host who is already playing something has answered that
question, and a file picker here would be a second way to get it wrong — worse, a way to start a
server whose own host cannot join it. The form asks only what it cannot know: name, port, player
limit, password.

The password is the one field deliberately not remembered between sessions. A password saved in a
config file that anyone with the machine can read is a worse promise than no password.

## Files

| File | What it holds |
|---|---|
| `computation/hostargs_compute.*` | What may go on a command line. A vector, never a string; hostile values dropped rather than escaped, since there is no legitimate map called `-iwad`. Windows quoting lives here because backslashes are only special before a quote, and that is the rule everybody implements wrongly from memory. |
| `computation/hostlifecycle_compute.*` | What state a host is in. Terminal is terminal, so a pipe draining after a failure cannot resurrect it. Readiness is observed, never assumed. |
| `zx_hostprocess.*` | Spawn, own, read, kill. The orphan guarantee. |
| `zx_hostwatchdog.*` | The child's half of that guarantee. |
| `zx_hosting.*` | The join between the three, plus readiness, secrecy and reachability. |

## Not verified here

The **positive** reachability path has been read out of the registry daemon and wired, but not
watched end to end — that needs a machine outside the NAT, which the development setup does not
have. The negative path (announce, hear nothing, report it) is verified.

macOS and Linux are written to the same design but built and run only on Windows so far.
