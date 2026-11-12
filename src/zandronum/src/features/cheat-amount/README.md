# Give/take cheat amounts

Adapts UZDoom's "extended give and take cheat's arguments to full integers"
(`uzdoom@a2a1d214c0bab6d9fbc564d7c4026a409608ac25`).

`give <item> <amount>` and `take <item> <amount>` used to cap the amount at 255 online (one byte in the
client command) and 32767 offline (a word in the net command). Both now carry an int, so `give Credits 100000`
gives 100000 when the item's `Inventory.MaxAmount` allows it.

- The console argument becomes the amount through `ComputeCheatAmountArg`: none is 0 (the command's default),
  anything else is clamped to [1, INT_MAX]. Upstream sends `atoi` raw; Zandronum always floored it at 1, and
  `strtoll` replaces `atoi`, whose overflow is undefined.
- Whoever applies it passes what it read through `ComputeCheatAmountFromWire`, so a negative amount (a hostile
  client, or the `-1` a truncated packet reads as) is "none given".
- `give health` adds and `give <armor> N` multiplies with `ComputeSaturatingAdd`/`ComputeSaturatingMul`, since
  INT_MAX health or armor overflowed before.

## Online

`CLC_GIVECHEAT` and `CLC_TAKECHEAT` send `Byte header, String item, Long amount` (was `Byte amount`). Client
and server must be the same build, which `NETGAMEVERSION` already enforces. The inventory, health and armor
the server sends back were already long or variable.

## Demos

`DEMOGAMEVERSION` is 0x21C. `DEM_GIVECHEAT`/`DEM_TAKECHEAT` record a long; a demo older than 0x21C still plays,
reading and skipping a word (`ComputeCheatAmountWireBytes`). Upstream changed the format without a bump.

## Hooks

- `c_cmds.cpp`: `SendGiveOrTake`, behind `CCMD (give)` and `CCMD (take)`.
- `cl_commands.cpp`: `CLIENTCOMMANDS_GiveCheat`, `CLIENTCOMMANDS_TakeCheat` write the long.
- `sv_main.cpp`: `server_GiveCheat` reads it.
- `d_net.cpp`: `Net_DoCommand` and `Net_SkipCommand` for `DEM_GIVECHEAT`/`DEM_TAKECHEAT`.
- `m_cheat.cpp`: `cht_Give` health, `GiveSpawner` armor.
- `version.h`: `DEMOGAMEVERSION`.

## Tests

- `CheatAmount.*` (`computation/cheatamount_compute_test.cpp`).
- `CheatAmountWire.*` (`cheatamount_wire_nettest.cpp`): golden bytes, round-trips past 255 and 32767,
  alignment of the next command, truncated and negative amounts.
