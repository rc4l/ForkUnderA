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

#endif // ZX_TRNSLATE_H
