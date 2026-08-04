# serverregistrylist-cdn

The Cloudflare Worker that serves [`serverregistries.txt`](../../serverregistries.txt) to clients.

```
GitHub (source of truth, edited by PR)
   ↓  fetched and cached, 6h
registrylist.cantstopscrolling.net   ← this Worker
   ↓  HTTPS, each client at most every 6h
client
   ↓  UDP, per server registry
registry.cantstopscrolling.net + others
```

GitHub holds the file so changes are reviewable in git; Cloudflare serves it so GitHub never sees
per-player traffic. Only the list of server *registries* travels this way — server lists themselves
are always UDP, straight from each server registry.

## Deploy

```
npm install -g wrangler
wrangler login
wrangler deploy
```

`wrangler login` is needed because deploying a Worker requires **account-level** permissions
(`Workers Scripts: Edit`), which a zone-scoped API token does not carry. If you would rather use a
token than an interactive login, it needs:

| scope | permission |
|---|---|
| Account | `Workers Scripts: Edit` |
| Zone | `Workers Routes: Edit` |
| Zone | `DNS: Edit` |

## Two things to set in the dashboard

Neither can be expressed in `wrangler.toml`, and both fail silently if missed.

**1. A proxied DNS record for the hostname.** Workers routes only fire on hostnames that resolve
into Cloudflare. Add an `A` record for `registrylist` pointing at `192.0.2.1` (a reserved
documentation address — it is never contacted, the Worker answers first) with the proxy **on**
(orange cloud).

This is the opposite of `registry.cantstopscrolling.net`, which must stay **grey**: Cloudflare
proxies HTTP, not UDP, so proxying a server registry silently blackholes it.

**2. Turn challenges off for this hostname.** A game client cannot solve a CAPTCHA or run a JS
interstitial. If Cloudflare challenges the request, the client receives an HTML page where it
expected a list — and the players it hits hardest are the ones on shared and CGNAT addresses, which
is most of the world outside the US.

Security → WAF → Custom rules, skip rule:

```
(http.host eq "registrylist.cantstopscrolling.net")   →   Skip: All remaining custom rules,
                                                          Super Bot Fight Mode, Browser Integrity Check
```

Also confirm Bot Fight Mode is not enabled zone-wide, or set Security Level to *Essentially Off* for
this hostname via a Configuration Rule.

The file is public, read-only, and a few hundred bytes. There is nothing here worth protecting from
bots, and every protection you add is an outage for someone.

## Verify

```
curl -sSi https://registrylist.cantstopscrolling.net/
```

Expect `200`, `content-type: text/plain`, and the file contents. Anything else — an HTML body, a
redirect, a challenge page — means one of the two dashboard settings above is wrong.

A client seeing that failure keeps its cached list and logs a warning; it never ends up with no
server registries. That is by design, and it also means a broken deploy here is easy to miss. Check
it after any zone-wide security change.

## Failure behaviour

The Worker answers `502` — never `200` with an empty or non-list body — when GitHub is unreachable,
returns non-2xx, or returns something starting with `<`. A client treats any non-list response as a
failed fetch and keeps what it had, so the worst case is a stale list, never an empty one.
