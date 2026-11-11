# Debug aim assist (`fua_debug_aimassist`)

A recorded fight measures what the shots set off (damage events, kills, the scripts behind them) only if the shots
land. A route steered from outside the engine re-aims a few times a second at best, since every bridge call waits for
a tic boundary mid-fight, and a third or more of a Minigun's bullets missed.

With `fua_debug_aimassist 1`, in single player and not while a demo plays back, each tic turns the local player's
ticcmd onto a monster before the demo records it:

- the target is the held one while it lives within `fua_debug_aimassist_range` (1500) and in sight, else the
  in-range monster in sight with the best score: its distance stretched by how far it is off the view;
- the turn reaches the target's middle from the shot's height in one tic, pitch held to the engine's -32..56 degrees;
  with no target the view returns to the heading, level;
- move input is taken along `fua_debug_aimassist_heading` (degrees, 0 east), not the view, so a scripted route keeps
  its path while the view turns.

The turn is in the recorded ticcmds, so a demo recorded with it plays back the same without the cvar. A route
recorder turns it on at the start of its firing lap, steers along the heading with no turning input of its own, and
turns it off at the end.

Off, it costs one cvar read a tic. It is not archived, so every session starts with it off. On, holding a target
costs a sight check a tic, and picking a new one walks every actor with up to 24 sight checks, in the handoff, so
only a recording pays it.

- `computation/aimassist_compute.*`: the aim, the tic's turn, the move on the heading and the targets' order.
- `aimassist.cpp`: the cvars, the target pick (sight checked, held by network id) and the ticcmd change.
- Hook: `g_game.cpp` `G_TickerBegin`, the console player's ticcmd before `G_WriteDemoTiccmd`, in the handoff while
  no tic runs.
