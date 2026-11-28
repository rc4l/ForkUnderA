// [Doruk-Aksoy] Named translations from TRNSLATE lumps (uzdoom@a505e910), which DECORATE's Translation and A_SetTranslation look up.
#ifndef ZX_TRNSLATE_H
#define ZX_TRNSLATE_H

#include "name.h"

// [Doruk-Aksoy] What R_FindCustomTranslation returns for a name nothing defines.
const int INVALID_TRANSLATION = -1;

// [Doruk-Aksoy] The translation a name stands for: None, Ice, Player1 to Player8, or a TRNSLATE definition.
int R_FindCustomTranslation(FName name);

// [Doruk-Aksoy] Reads every TRNSLATE lump into TRANSLATION_Custom, after the standard tables and before DECORATE.
void R_ParseTrnslate();

class AActor;
struct FRemapTable;

namespace zx { namespace trnslate {

struct TranslationRange;

// [Doruk-Aksoy] Adds one range of any kind to `table`, as a script's TRANSLATIONRANGE did on the server and a client's copy does from its send.
void AddTranslationRange(FRemapTable *table, const TranslationRange &range);

// [Doruk-Aksoy] Gives `actor` translation `tnum` (its class's with `resetToDefault`; an unknown one changes nothing), a server telling the clients; `localOnly`, for a clientside script, changes this machine's copy alone.
void SetActorTranslation(AActor *actor, bool resetToDefault, int tnum, bool localOnly);

}} // namespace zx::trnslate

#endif // ZX_TRNSLATE_H
