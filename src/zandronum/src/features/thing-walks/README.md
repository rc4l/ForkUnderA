# Thing walks a mover can skip

`P_CheckPosition` walks every thing in the blocks a move touches and asks `PIT_CheckThing` about
each. For a mover with `THRUACTORS`, every one of those calls returns before it changes anything, so
the walk is skipped. Mods spawn effect actors that way (sparks, trails), and on Sunder MAP11 with
Complex Doom each one walked the crowd around it every tic.

- `computation/thingwalk_compute.*`: the rule.
- Hooks: `p_map.cpp` `P_CheckPosition` (the thing walk) and `P_TestMobjZ` (what it stands on), which skip for such a mover.
