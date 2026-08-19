# The Server Registry

A registry is a phone book. Servers write themselves into it, players read it, and neither has to know the other exists beforehand. ForkUnderA ships pointing at one run by rc4l, but the phone book is not owned by anybody. A registry is a small program anyone can run, and the client will read from several at once.

```mermaid
sequenceDiagram
    autonumber
    participant S as Server
    participant NS as Server's router
    participant R as Registry
    participant NP as Player's router
    participant P as Player

    Note over S,P: Routers appear only where they change the outcome

    rect rgba(56, 139, 253, 0.15)
        Note over S,R: 1. Listing, done once down each internet
        par Over IPv4
            S->>NS: announce
            NS->>R: arrives wearing the household address
            R-->>NS: a random cookie
            NS-->>S: a random cookie
            S->>NS: the cookie, returned
            NS->>R: the cookie, returned
        and Over IPv6
            Note over S,R: No router in the way, only a firewall
            S->>R: announce
            R-->>S: a random cookie
            S->>R: the cookie, returned
        end
        Note over R: Silence means a spoofed address, so the listing is dropped.<br/>A returned cookie proves the address is real.<br/>It does not prove strangers can get in.
        Note over R: Same server key and same port on both sides<br/>means one server holding two addresses.<br/>Anything else is listed as two.
    end

    rect rgba(63, 185, 80, 0.15)
        Note over R,P: 2. Browsing
        P->>R: send me the list
        R-->>P: the servers, with each pair of addresses tied together
        Note over P: Draws one row per server and speaks<br/>whichever family it actually has
    end

    rect rgba(210, 153, 34, 0.15)
        Note over S,P: 3. Joining
        alt The server is reachable
            P->>S: challenge
            S-->>P: server info, then the join
        else The server sits behind a router
            P->>R: I cannot reach them
            R->>S: someone is about to arrive, here is where from
            par Each side opens its own router from the inside
                S->>NS: a packet aimed at the player
                NS--)NP: a knock nothing is listening for
            and
                P->>NP: challenge, retried with widening gaps
                NP->>NS: arrives while the opening is still held
            end
            NS->>S: challenge
            S-->>P: server info, then the join
            Note over NS,NP: A race. Whichever packet lands first takes the opening<br/>the other side was aiming at, which is why the gaps<br/>between retries are uneven rather than regular.
        end
    end
```
