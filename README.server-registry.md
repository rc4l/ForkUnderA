# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
sequenceDiagram
    autonumber
    participant S as Your Server
    participant R as Registry
    participant P as Player

    rect rgba(56, 139, 253, 0.15)
        Note over S,R: Getting listed
        S->>R: I exist, here is my name and map
        R->>S: Prove it, here is a random cookie
        S->>R: The cookie, sent back
        Note over R: Silence means a spoofed address.<br/>The listing is dropped.
    end

    rect rgba(63, 185, 80, 0.15)
        Note over P,R: Finding you
        P->>R: Who is out there?
        R->>P: A list, your server among them
    end

    rect rgba(210, 153, 34, 0.15)
        Note over S,P: Joining through a router
        P->>R: I want in, but I cannot reach them
        R->>S: Someone is about to knock
        S-->>P: Knock (opens the router)
        P->>S: Join
    end
```
