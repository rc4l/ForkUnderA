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
    class R4,Q4,A3,B4,C3,G1,G2,G3,G4 ok
    class S3,E1,E2,E3 warn

    style R fill:none,stroke:#94a3b8
    style Q fill:none,stroke:#94a3b8
    style A fill:none,stroke:#94a3b8
    style B fill:none,stroke:#94a3b8
    style C fill:none,stroke:#94a3b8
    style S fill:none,stroke:#94a3b8
```

## Terminology

| Term | What it means |
|---|---|
| **Datagram** | A single UDP packet. It is sent and either arrives or does not, with no handshake and no acknowledgement, which is why every mechanism below is about who is allowed to send one to whom. |
| **Announce** | A server telling a registry it exists. Repeated at intervals, because a listing that is never refreshed is indistinguishable from a server that has gone away. |
| **Nonce** | A number used once. The registry invents one and will only list a server that sends the same number back, which proves the announcement came from the address it claimed rather than from someone typing that address into a packet. |
| **Endpoint** | An address and a port together. The distinction matters here because NAT rewrites both, so the endpoint a server believes it has and the one the world sees are frequently different. |
| **A and AAAA** | The two kinds of DNS record that answer "where is this host". A gives an IPv4 address, AAAA an IPv6 one. A name can have both, one, or neither, and asking for the wrong one is how a working host looks unreachable. |
| **Dual-stack** | Running IPv4 and IPv6 at the same time. A dual-stack server is two addresses for one machine, which is why the browser has to recognise the pair and draw one row. |
| **Server key** | The public half of the identity a server presents. Combined with its port it is what lets the registry tell "one server on two addresses" from "two servers". |
| **NAT** | Network Address Translation. A router presenting many private addresses to the internet as one public address, rewriting the endpoint of everything passing through. It is what made IPv4 survive running out of addresses, and what makes an unforwarded server unreachable. |
| **Mapping** | The temporary hole NAT creates when something inside sends out. It admits replies from that destination for a while, then expires. Nothing inbound arrives without one. |
| **Unsolicited inbound** | A packet arriving from someone the machine has not recently sent to. Both NAT and firewalls drop it by default, which is the entire problem the registry exists to work around. |
| **Stateful firewall** | A firewall that permits inbound traffic only where it matches something previously sent out. IPv6 hosts usually sit behind one, so a global address still does not mean a reachable one. |
| **Rendezvous** | An introduction performed by a third party. The registry can reach both sides, so it tells each about the other at the same moment, which neither could have learned alone. |
| **Simultaneous open** | Both ends sending outward at once so that each opens its own mapping. Neither packet is expected to be answered. Their only purpose is to make the router treat the reply as solicited. |
| **Symmetric NAT** | NAT that allocates a fresh mapping per destination rather than per source. The mapping opened toward the registry therefore tells nobody which mapping a peer should aim at, so no amount of introduction helps. |
| **Port forwarding** | A permanent, manually configured mapping. It is the one method that always works, at the cost of having to be set up by hand. |
