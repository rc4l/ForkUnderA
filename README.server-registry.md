# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
stateDiagram-v2
    direction TB

    state "1. The server announces itself" as Announcing
    state "2. Proving the address is real" as Proving
    state "3. Listed, one row per server" as Listed
    state "4. Punching through both routers" as Punching
    state "Playing" as Joined
    state "Never listed" as Dropped
    state "Unreachable" as Refused

    Announcing --> Proving : over IPv4 through the router,<br/>IPv6 through the firewall
    Proving --> Dropped : the number never comes back
    Proving --> Listed : the number comes back<br/>from the same address
    Listed --> Joined : the player reaches it directly
    Listed --> Punching : no reply, because a home<br/>router keeps no door open
    Punching --> Joined : both sides send outward at once,<br/>until one lands while the far end holds
    Punching --> Refused : a fresh port per destination,<br/>so forward the port instead

    classDef good fill:#dcfce7,stroke:#16a34a,color:#14532d
    classDef bad fill:#fee2e2,stroke:#dc2626,color:#7f1d1d
    classDef live fill:#dbeafe,stroke:#2563eb,color:#1e3a8a
    class Joined good
    class Dropped bad
    class Refused bad
    class Listed live
```
