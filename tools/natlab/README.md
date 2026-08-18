# natlab

**Problem:** a server can compile, boot, pass every unit test and still be unjoinable, because the
one thing that decides whether players can reach each other is a router neither we nor they control.
The only test that ever covered it was a person with a laptop on a phone hotspot, run by hand, which
is not something you can do per release.

**Solution:** two engines behind two simulated home routers, with a registry in the middle, in
containers, in CI.

```mermaid
flowchart LR
    subgraph LH["lan_host (isolated)"]
        H(["host<br/>forkundera -host"])
    end
    subgraph LC["lan_client (isolated)"]
        C(["client<br/>forkundera"])
    end
    subgraph PUB["pub — the internet"]
        REG[("registry")]
    end

    H --- RH(["router_host<br/>MASQUERADE + DROP inbound"])
    C --- RC(["router_client<br/>MASQUERADE + DROP inbound"])
    RH --> REG
    RC --> REG
    RH -. "punch" .-> RC

    classDef net fill:#1f2933,stroke:#3e4c59,color:#e4e7eb,rx:8,ry:8
    classDef box fill:#2b3a42,stroke:#52606d,color:#f5f7fa,rx:10,ry:10
    class H,C,RH,RC,REG box
```

Neither peer is on `pub`. Their LANs are `internal: true` and their routers drop unsolicited inbound,
so **a connection that succeeds could only have been carried by the mechanism under test**.

## Run it

```sh
ZX_MCP_BRIDGE=1 ./linux_compile.sh --no-package     # the lab drives the engine through the bridge
tools/natlab/run.sh                                  # both ends port-restricted
tools/natlab/run.sh --host-nat symmetric             # the case punching cannot solve
tools/natlab/run.sh --keep                           # leave it up to poke at
```

Linux only: it needs real network namespaces.

## What it asserts

| # | Assertion | Why it is there |
|---|---|---|
| 0 | the peers **cannot** ping each other | without this the rest is vacuous |
| 1 | the host is listed by the registry | outbound works through NAT |
| 2 | the host is **not** verified | the NAT is real, and `hostdiag` says so honestly |
| 3 | the client finds the server via the registry | discovery, not LAN broadcast |
| 4 | the **server** reports a connected client | the only proof a punch carried something |

Assertion 0 is the important one. Delete the `DROP` rule in `router.sh` and every other assertion
still passes while proving nothing, so the fixture is checked before it is trusted.

## Symmetric NAT

Expected to fail the punch, and the lab says so rather than pretending otherwise: the mapping the
registry observed is not the mapping the host is told to aim at. What is asserted there is clean
degradation — the client must not hang — because a wedged client is the failure a player would feel.
