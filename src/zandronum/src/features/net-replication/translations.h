// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] net-replication's public header for the ACS translations a server's scripts build.

#ifndef ZX_NETREPLICATION_TRANSLATIONS_H
#define ZX_NETREPLICATION_TRANSLATIONS_H

namespace zx { namespace trnslate { struct TranslationRange; } }

namespace zx { namespace netreplication {

// [Doruk-Aksoy] A range a server script added to ACS translation `index` (from 1): sent to every client and kept, so a joiner gets it again.
void SendTranslationRange(int index, const trnslate::TranslationRange &range);

// [Doruk-Aksoy] The end of a server script's ACS translation `index`: one that took no range since it started is the identity, which the clients and a joiner
// learn from a range mapping entry 1 to itself (which leaves an identity table as it is), where they kept whatever table they had.
void SendTranslationEnd(int index);

}} // namespace zx::netreplication

#endif // ZX_NETREPLICATION_TRANSLATIONS_H
