# Looking for players (`P_LookForPlayers`)

A monster looking for a target tries the players in a random order. Zandronum's rewrite of
`P_LookForPlayers` drew `M_Random(MAXPLAYERS)` until every one of the 64 slots had come up, and
rescanned all 64 after each draw, so a look that found nobody took about 300 draws. A netgame
monster that loses sight of its target looks again on every chase step, which made this 15-25% of a
busy server's tic on Sunder MAP11.

The order is now drawn over the players in the game only: each untried player is equally likely
next, as before, with one draw per player tried and none for the last. The game plays the same way
by chance, but `M_Random` is drawn fewer times, so demos recorded before this play out differently.

- `computation/lookorder_compute.*`: the order (`ComputeLookOrderStart`, `ComputeLookOrderNext`).
- Hook: `p_enemy.cpp` `P_LookForPlayers`, the player loop.
