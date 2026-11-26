# features/acs-fields

ACS field access by name through an actor handle, `fua_` functions 24030-24031. Modder reference:
[`docs/modding/acs/Actor_fields.md`](../../../../../docs/modding/acs/Actor_fields.md).

- A name resolves through the class's symbol table, which DECORATE fills from `actor.txt`'s native
  declarations and each class's `user_` variables, the same lookup `GetUserVariable` uses.
- `ComputeFieldShape` maps DECORATE's value type to how the field is stored; `ComputeFieldRead` turns a
  48.16 `fixed_t` into ACS fixed point (its low 32 bits, as `GetActorX`), an `angle_t` into an ACS angle
  (as `GetActorAngle`), and reads arrays by index.
- Writes reach int `user_` variables and arrays only, so nothing needs sending; like `SetUserVariable`'s,
  they stay on the machine that made them.

## Hooks

- `p_acs.cpp`: the two functions.
- `features/acs-argcount`: each function's minimum argument count.

## Tests

- `computation/fields_compute_test.cpp`: shapes, indices, the fixed, angle and double conversions, reads
  of each kind and the write rules.
- `fuactl scenarios acsfields`: native fields against `GetActorX`, `GetActorAngle` and the map, an array
  by index, a far actor's truncated x matching `GetActorX`, and writes to `user_` variables only.
- `fuactl scenarios netacsfields`: the same checks as CLIENTSIDE copies on the player's client, an
  observer and a late joiner, then on the server, each on its own actors; the server reads the `user_`
  variables the clients wrote on their copies as 0, since a client's write stays on that client.
