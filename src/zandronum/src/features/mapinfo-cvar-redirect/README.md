# mapinfo-cvar-redirect

Ports the `cvar_redirect` MAPINFO map keyword from UZDoom.

- **Keyword:** `cvar_redirect = "<cvarname>", "<map>"`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@04ea28def (companion uzdoom@4ac76d82d)

## Behavior

Mirrors the existing `redirect` keyword, but instead of redirecting when a
player carries a given inventory item, it redirects to `<map>` when the named
CVAR evaluates to a non-zero integer. Both mechanisms can coexist on one map;
the item redirect is checked first, then the CVAR redirect.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `level_info_t::RedirectCVAR` (FName) +
  `RedirectCVARMap[9]` fields, next to `RedirectType`/`RedirectMap`.
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — zero the two new fields.
  - `level_info_t::CheckLevelRedirect()` — CVAR branch after the item branch,
    using `FindCVar` + `GetGenericRep(CVAR_Int)`.
  - `DEFINE_MAP_OPTION(cvar_redirect)` — parse handler next to `redirect`.
  - `#include "c_cvars.h"` added for the CVAR API.
