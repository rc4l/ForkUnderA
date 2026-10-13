# cvar-alias

A cvar known by a `fua_` name as well as its own, so `fua_vid_fps` is `vid_fps`: setting, reading,
`toggle`, binds and config lines all reach the one cvar, which keeps its own name in what the console
prints. The table is `computation/cvaralias_compute.cpp`, one line per alias.

## In-engine hooks

- `c_cvars.cpp` `FindCVar` and `FindCVarSub`: a name no cvar has is looked up in the alias table
  (`ComputeCVarAliasTarget`) and answers with its target.
