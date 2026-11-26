# Timed demos

`-timedemo <demo>` plays a demo as fast as the machine can and reports how long it took. Upstream reported that
through `I_FatalError`, whose modal box on Windows waited for someone to close it, so a timing run could not end on
its own. The line is now printed and a `quit` is queued for the handoff (`features/handoff`), which runs it on the
main thread between tics, as typing `quit` does, with the game thread on or off.

`computation/timedemo_compute.*`: the line, worded as before. Hook: `g_game.cpp` (`G_CheckDemoStatus`).
