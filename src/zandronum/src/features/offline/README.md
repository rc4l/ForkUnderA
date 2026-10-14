# offline

A build that plays offline only: `ZX_ONLINE` off leaves out the features that reach other machines, and `offlinestubs.cpp` answers where shared code calls into them. `docs/web-port-design.md` (5e, 5f) has the plan. The browser build and the `linux-offline` preset are built this way.

- `ONLINE_SOURCES` in `src/CMakeLists.txt` lists what is left out: accounts, the HTTP layer, wad downloads and serving, hosting, the server browser and the updater, among others.
- `offlinestubs.cpp` gives each entry point shared code links against the answer of a build with nothing to reach: an empty server list, no host, no account, no download, no update. The list grows only from the linker's evidence, so a new call into an online feature fails the offline link until it gets an answer here.
- The server browser's menu class, which MENUDEF names, is a stand-in saying the build plays offline.

Wad hashing (`zx_filehash`) and the search of the player's own folders (`zx_wadsearch`) stay in every build.

## In-engine hooks

- `src/CMakeLists.txt`: `ONLINE_SOURCES`, replaced by `offlinestubs.cpp` when `ZX_ONLINE` is off.
- `CMakeLists.txt`: the `ZX_ONLINE` option.
- `features/global-header/zx_globalheader.cpp`: an offline launch greys "Play Online!" without asking the network (`HeaderReach::OfflineLaunch`).
- `network.cpp`: `NETWORK_GetLocalAddress` reports no address for an offline launch, which has no socket to ask.
