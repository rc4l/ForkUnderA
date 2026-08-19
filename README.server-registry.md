# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
flowchart TB

    subgraph L["① Getting listed"]
        direction LR
        L1["Your server"] -->|"announces over IPv4<br/>and over IPv6"| L2["Registry"]
        L2 -->|"sends back a<br/>random number"| L3["Your server<br/>returns it"]
        L3 --> L4(["Listed, one row"])
    end

    subgraph F["② Finding it"]
        direction LR
        F1["A player"] -->|"asks for the list"| F2["Registry"]
        F2 -->|"every server it holds"| F3(["One row each, both<br/>addresses collapsed"])
    end

    subgraph D["③ Joining, the easy way"]
        direction LR
        D1["The player"] -->|"knocks"| D2["Your server"]
        D2 -->|"answers"| D3(["Playing"])
    end

    subgraph P["④ Joining through a router"]
        direction LR
        P1["The player"] -->|"knocks, no answer"| P2["Registry"]
        P2 -->|"tells the server<br/>who is coming"| P3["Both sides send<br/>outward at once"]
        P3 -->|"one lands while the<br/>far side is still open"| P4(["Playing"])
        P3 -->|"the router changes<br/>port every time"| P5(["Forward the port"])
    end

    L ~~~ F
    F ~~~ D
    D ~~~ P

    classDef ok fill:#dcfce7,stroke:#16a34a,color:#14532d
    classDef warn fill:#fef3c7,stroke:#d97706,color:#78350f
    class L4,F3,D3,P4 ok
    class P5 warn

    style L fill:#f8fafc,stroke:#cbd5e1,color:#0f172a
    style F fill:#f8fafc,stroke:#cbd5e1,color:#0f172a
    style D fill:#f8fafc,stroke:#cbd5e1,color:#0f172a
    style P fill:#f8fafc,stroke:#cbd5e1,color:#0f172a
```
