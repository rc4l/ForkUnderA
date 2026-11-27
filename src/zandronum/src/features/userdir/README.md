# userdir

Where the per-user folder lives. It holds the identity keys, the Continue records, and the IWAD and PWAD stores.

By default it sits beside the node cache (`%LOCALAPPDATA%/ForkUnderA` on Windows). The launch switch `-fua_userdir <path>` puts it somewhere else. fuactl gives every driven launch a folder of its own this way. Without it, every test engine shared the player's folder: tests wrote Continue records into the player's slots, copied test wads into their PWAD store, and a test client that disconnected was sent back to whatever an earlier test had left in its slot.

## Files

- `computation/userdir_compute.{h,cpp}`: `ComputeFuaUserPath`, the folder from the switch or the cache path.

## In-engine hooks

- `m_specialpaths.cpp` `M_GetFuaUserPath`: reads `-fua_userdir` and calls `ComputeFuaUserPath`.
