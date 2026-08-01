**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`state A_JumpIfInput (int keys, state label, int flags = 0, int owner = AAPTR_DEFAULT)`

# A_JumpIfInput

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2, and has no GZDoom equivalent — GZDoom
> exposes raw input through the `GetPlayerInput` expression function instead, which this engine
> does not have.
>
> **Works in netgames**, unlike the [`A_Overlay` family](Psprite_layers.md). The jump is decided
> by the server and replicated, so it is consistent for everyone — at the cost of latency, see
> [below](#latency).

## Usage

Jumps to `label` when a player is holding the buttons named in `keys`.

Before this, DECORATE could not see input at all. Reacting to a button mid-animation meant
running a client-side ACS script that polled `GetPlayerInput` and pushed the actor into a state
from outside — awkward to write, and easy to desync. `A_JumpIfInput` puts the test in the state
sequence where the branch actually happens.

The usual application is branching out of a loop that is already running: letting a player
interrupt a wind-up by releasing fire, tap altfire mid-`Fire` sequence to switch firing modes,
or hold a movement key to change a melee attack's direction.

By default the test passes while **any** listed button is held, and it is re-tested every tic
the state runs. `JIF_ALL`, `JIF_EDGE` and `JIF_NOT` change that.

Being a jump function, this never sets the result of an inventory state chain — a
`CustomInventory` whose Pickup state calls it will not have the pickup cancelled by a failed
test.

### Parameters

- `int keys`

  An OR-mask of the `BT_*` button constants to test, combined with `|` — for example
  `BT_ATTACK | BT_ALTATTACK`.

  **A mask of `0` never matches, even with `JIF_NOT`.** The empty-mask guard runs before the
  inversion, so `A_JumpIfInput(0, "Somewhere", JIF_NOT)` never jumps rather than always jumping.

  These eight are the gameplay buttons. They already reach the server every tic, so they cost
  nothing extra to test:

  | Constant | Value | Button |
  |---|---|---|
  | `BT_ATTACK` | 1 | Fire |
  | `BT_USE` | 2 | Use / open |
  | `BT_JUMP` | 4 | Jump |
  | `BT_CROUCH` | 8 | Crouch |
  | `BT_TURN180` | 16 | Turn 180° |
  | `BT_ALTATTACK` | 32 | Alt fire |
  | `BT_RELOAD` | 64 | Reload |
  | `BT_ZOOM` | 128 | Zoom |

  The rest are movement, look and script buttons. They are **not** transmitted every tic
  normally; see [Network cost](#network-cost).

  | Constant | Value | Button |
  |---|---|---|
  | `BT_SPEED` | 256 | Run |
  | `BT_STRAFE` | 512 | Strafe modifier |
  | `BT_MOVERIGHT` | 1024 | Strafe right |
  | `BT_MOVELEFT` | 2048 | Strafe left |
  | `BT_BACK` | 4096 | Move back |
  | `BT_FORWARD` | 8192 | Move forward |
  | `BT_RIGHT` | 16384 | Turn right |
  | `BT_LEFT` | 32768 | Turn left |
  | `BT_LOOKUP` | 65536 | Look up |
  | `BT_LOOKDOWN` | 131072 | Look down |
  | `BT_MOVEUP` | 262144 | Move up (swim / fly) |
  | `BT_MOVEDOWN` | 524288 | Move down (swim / fly) |
  | `BT_SHOWSCORES` | 1048576 | Show scoreboard |
  | `BT_USER1` | 2097152 | `+user1` |
  | `BT_USER2` | 4194304 | `+user2` |
  | `BT_USER3` | 8388608 | `+user3` |
  | `BT_USER4` | 16777216 | `+user4` |

- `state label`

  Where to jump when the test passes. A quoted state name (`"AltFire"`) or a bare numeric
  offset from the current state. `0` or `"None"` means no jump, which makes the call a no-op.

- `int flags`

  Multiple flags can be combined with `|`. The following are available:

  - **JIF_ALL** — require **every** button in `keys` to be held at once, instead of any single
    one. `A_JumpIfInput(BT_ATTACK|BT_ALTATTACK, "Both", JIF_ALL)` fires only while both are down.

  - **JIF_EDGE** — match only on the tic the input becomes **newly** satisfied, not for as long
    as it is held. This is what distinguishes a tap from a hold: without it, a state that loops
    while the player holds altfire will jump on every tic.

    With `JIF_ALL`, the edge is the tic the *combination* completes — the last button of the
    combo going down — not each individual press.

  - **JIF_NOT** — invert the result, jumping when the test is **not** satisfied. Applied last,
    after `JIF_ALL` and `JIF_EDGE`, so `JIF_EDGE|JIF_NOT` jumps on every tic that is *not* a
    rising edge, which is rarely what you want. Pair `JIF_NOT` with the default or `JIF_ALL`.

- `int owner`

  An `AAPTR_*` pointer selector choosing **whose** input to read. Defaults to `AAPTR_DEFAULT`,
  the calling actor's own player.

  Use it to have one actor react to another's input — a deployed turret reading its owner's
  buttons via `AAPTR_MASTER`, for example. If the selected pointer is null or is not a player,
  no jump happens.

## Latency

The server decides the jump, so for the player who pressed the button it lands roughly one
round-trip after the press, rather than instantly. Real fire and altfire feel immediate because
they are client-predicted; `A_JumpIfInput` is not. Keep that in mind for anything meant to feel
tight — a parry window sized in single tics will not survive the trip.

Actors flagged `+CLIENTSIDEONLY` are exempt: they evaluate the jump locally, with no latency and
no replication.

## Network cost

The eight gameplay buttons above are in the low byte of the button word and are sent every tic
already, so testing them costs nothing.

Everything above that byte — movement, look, `BT_SPEED`, and `BT_USER1`–`BT_USER4` — is not
normally transmitted. So that the server can see them, the client now sends the **full 32-bit**
button set on any tic where one of them is held. That costs a few extra bytes per tic, and only
while such a button is actually down.

## Examples

A super shotgun that lets the player interrupt the reload by tapping altfire, dumping the second
shell for a quicker recovery. `JIF_EDGE` is what makes this a tap rather than a hold — without
it the jump would trigger on the first tic altfire is down and every tic after.

```
ACTOR InterruptibleSSG : SuperShotgun
{
  States
  {
  Fire:
    SHT2 A 3
    SHT2 A 7 A_FireShotgun2
    SHT2 B 7
    SHT2 C 7 A_CheckReload
    SHT2 D 7 A_JumpIfInput(BT_ALTATTACK, "QuickEject", JIF_EDGE)
    SHT2 EFG 6
    SHT2 H 6 A_Refire
    Goto Ready

  QuickEject:
    SHT2 EF 3
    SHT2 H 4
    Goto Ready
  }
}
```

A charged attack that releases when the player **lets go** of fire, using `JIF_NOT` against the
default "any button held" test. The charge loop runs for as long as fire is down; the first tic
it is not, the jump fires.

```
  Fire:
    PLSG A 2 A_JumpIfInput(BT_ATTACK, "Release", JIF_NOT)
    PLSG B 2 A_JumpIfInput(BT_ATTACK, "Release", JIF_NOT)
    PLSG A 0 A_JumpIfInput(BT_ATTACK, "Fire", JIF_NOT)
    Goto Fire

  Release:
    PLSG C 4 A_FireProjectile("PlasmaBall")
    PLSG D 4
    Goto Ready
```

A melee attack whose finisher depends on the player holding forward and run together — a
`JIF_ALL` combination test:

```
  Melee:
    PUNG B 4
    PUNG C 4 A_JumpIfInput(BT_FORWARD|BT_SPEED, "Lunge", JIF_ALL)
    PUNG D 4 A_CustomPunch(20)
    Goto Ready

  Lunge:
    PUNG C 2 A_Recoil(-8)
    PUNG D 4 A_CustomPunch(40)
    Goto Ready
```

Note that `BT_FORWARD` and `BT_SPEED` both sit above the low byte, so this test is one of the
cases that makes the client send the full button word while those keys are down.

## See also

- [A_JumpIf](https://zdoom.org/wiki/A_JumpIf) — jump on an arbitrary DECORATE expression
- [Actor pointers](https://zdoom.org/wiki/Actor_pointer) — the `AAPTR_*` selectors the `owner`
  parameter takes
- [Psprite layers](Psprite_layers.md) — note that a jump issued from an overlay layer is not
  replicated, so it is single-player only
