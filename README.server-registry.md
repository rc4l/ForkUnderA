# The Server Registry

A Server Registry is what we'd formerly call a Master Server. It used to be effectively a dumb application hosted on the cloud that only serves a list of ip addresses to Game Servers for valid users. With ForkUnderA, it's upgraded to a verifying, federated broker: it challenges every announcement so a listing cannot be forged, speaks IPv4 and IPv6 as one, and introduces players directly to servers whose owners never touched a router setting. Anyone can run one in a container and point their community at it, so the list is no longer a single service everybody has to trust or wait on.

Below are some high level architecture diagrams on how it all works.

```mermaid
flowchart TB

    subgraph R["Server Registry and Game Server handshake"]
        direction LR
        R1["Game Server"] -->|"tries to broadcast,<br/>over IPv4 and IPv6"| R2["Server Registry"]
        R2 -->|"sends a random num"| R3["Game Server<br/>sends it back"]
        R3 --> |"numbers match?"| R4(["Game Server is verified on the Server Registry"])
        R3 --> |"numbers don't match?"| E1(["Game Server not verified, won't broadcast"])
        R3 --> |"Lag?"| E2(["Automatically retries this flow again"])
    end

    subgraph Q["Game Server Discovery"]
        direction LR
          Q1["Client browser"] -->|"queries every server registry it knows about"| Q2["Server registries<br/>(one or more)"]
          Q2 -->|"returns the IPv4 and IPv6<br/>addresses it holds"| Q1
          Q1 -->|"queries each address directly"| WWW["Game servers"]
          WWW -->|"respond with data (name, wads, etc)"| Q1
          Q1 -->|"happy path <br/>(not banned, version match, etc)"| G1(["One row per server,<br/>dual-stack pair collapsed"])
          Q1 -->|"sad path <br/>(banned, bad version, etc)"| E3(["Row hidden"])
    end

    subgraph A["Client connects to a Game Server. Scenario A: IPv4, port forwarded"]
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
    class R4,Q4,A3,B4,C3,G1,G2,G3,G4 ok
    class S3,E1,E2,E3 warn

    style R fill:none,stroke:#94a3b8
    style Q fill:none,stroke:#94a3b8
    style A fill:none,stroke:#94a3b8
    style B fill:none,stroke:#94a3b8
    style C fill:none,stroke:#94a3b8
    style S fill:none,stroke:#94a3b8
```
