# net

Network helpers shared by the features that reach outside the game's own UDP:

- `zx_httpsget` and `zx_httpfile`: one HTTPS GET, as text or streamed to a file, each with its platform backends behind one call.
- `zx_resolve`: a host resolved to an IPv6 address, which `NETADDRESS_s::LoadFromString` can't do.
- `zx_offline`: `OfflineLaunch()`, true for a launch given `-offline`, which the browser build always is (`features/web`). Such a launch opens no socket, asks no server registry and checks for no update.
- `computation/`: the address arithmetic behind them (`hostliteral`, `v6mapped`, `v6prefix`), tested.

## In-engine hooks

- `network.cpp`: `NETWORK_Construct` skips `network_OpenSockets` for an offline launch, and every send and receive then finds no socket.
- `features/server-browser/browser.cpp`: the background registry query is skipped offline.
- `features/updater/zx_updater.cpp`: the update check is skipped offline.
