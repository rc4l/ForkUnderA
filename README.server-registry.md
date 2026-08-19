# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
stateDiagram-v2
    direction TB

    [*] --> Silent

    Silent --> Announced : hosting begins, and the server says so<br/>down both internets at once
    Announced --> Proving : the registry replies with a random number
    Proving --> Silent : it never comes back, so the address<br/>was a forgery
    Proving --> Listed : it comes back from the same address,<br/>which only the real occupant could manage
    Listed --> Silent : the server stops announcing

    Listed --> Joined : a player reaches the server directly
    Listed --> Knocking : a player gets no reply, because a home<br/>router keeps no door open

    Knocking --> Punching : the registry tells the server who is<br/>arriving, and from where
    Punching --> Joined : both sides send outward at once, each<br/>opening its own router, until a packet<br/>lands while the far opening still holds
    Punching --> Refused : the router hands out a fresh port per<br/>destination, so the opening is never<br/>the one aimed at

    Joined --> [*]
    Refused --> [*]

    note right of Listed : IPv4 leaves through the router, IPv6 through<br/>the firewall. Two addresses sharing a key and<br/>a port are one server and draw one row.
    note right of Refused : Symmetric NAT, common on mobile.<br/>Port forwarding is the certain answer.

    classDef good fill:#dcfce7,stroke:#16a34a,color:#14532d
    classDef bad fill:#fee2e2,stroke:#dc2626,color:#7f1d1d
    classDef live fill:#dbeafe,stroke:#2563eb,color:#1e3a8a
    class Joined good
    class Refused bad
    class Listed live
```
