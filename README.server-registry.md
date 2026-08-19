# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
flowchart TB

    subgraph R["① Registration"]
        direction LR
        R1["Server"] -->|"announces on UDP 15300,<br/>over IPv4 and IPv6"| R2["Registry"]
        R2 -->|"challenges with a nonce"| R3["Server echoes it from<br/>the same endpoint"]
        R3 --> R4(["Listing verified"])
    end

    subgraph Q["② Discovery"]
        direction LR
        Q1["Client"] -->|"resolves A and AAAA,<br/>queries both"| Q2["Registry"]
        Q2 -->|"returns the server list"| Q3["Client collapses the dual-stack<br/>pair on server key and port"]
        Q3 --> Q4(["One row per server"])
    end

    subgraph A["③ IPv4, port forwarded"]
        direction LR
        A1["Client"] -->|"unsolicited inbound,<br/>permitted by the forward"| A2["Server"]
        A2 --> A3(["Direct connection"])
    end

    subgraph B["④ IPv4 behind NAT"]
        direction LR
        B1["Client"] -->|"no inbound mapping exists,<br/>so the datagram is dropped"| B2["Registry brokers<br/>a rendezvous"]
        B2 -->|"simultaneous open"| B3["Both endpoints emit outbound,<br/>each creating its own mapping"]
        B3 --> B4(["Connection established"])
    end

    subgraph C["⑤ IPv6, stateful firewall"]
        direction LR
        C1["Client"] -->|"global address, no NAT, but<br/>unsolicited inbound is dropped"| C2["Registry brokers<br/>a rendezvous"]
        C2 -->|"outbound creates<br/>firewall state"| C3(["Connection established"])
    end

    subgraph S["⑥ Symmetric NAT"]
        direction LR
        S1["Client"] -->|"mapping is allocated<br/>per destination"| S2["The punched port is never<br/>the one the peer targets"]
        S2 --> S3(["Port forwarding required"])
    end

    R ~~~ Q
    Q ~~~ A
    A ~~~ B
    B ~~~ C
    C ~~~ S

    classDef ok fill:none,stroke:#16a34a,stroke-width:2px
    classDef warn fill:none,stroke:#d97706,stroke-width:2px
    class R4,Q4,A3,B4,C3 ok
    class S3 warn

    style R fill:none,stroke:#94a3b8
    style Q fill:none,stroke:#94a3b8
    style A fill:none,stroke:#94a3b8
    style B fill:none,stroke:#94a3b8
    style C fill:none,stroke:#94a3b8
    style S fill:none,stroke:#94a3b8
```
