# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
stateDiagram-v2
    direction TB

    state "Listed as one row" as Listed

    [*] --> Silent

    Silent --> Proving : announces over IPv4 through the router and<br/>IPv6 through the firewall, and the registry<br/>answers with a random number
    Proving --> Silent : the number never returns,<br/>so the address was forged
    Proving --> Listed : the number returns from the same address,<br/>and both carry one key and one port
    Listed --> Silent : announcing stops

    Listed --> Joined : the player reaches it directly
    Listed --> Punching : no reply, so the registry<br/>brokers a knock
    Punching --> Joined : a packet lands while<br/>the opening still holds
    Punching --> Refused : a fresh port per destination,<br/>so forward the port instead

    Joined --> [*]
    Refused --> [*]

    classDef good fill:#dcfce7,stroke:#16a34a,color:#14532d
    classDef bad fill:#fee2e2,stroke:#dc2626,color:#7f1d1d
    classDef live fill:#dbeafe,stroke:#2563eb,color:#1e3a8a
    class Joined good
    class Refused bad
    class Listed live
```
