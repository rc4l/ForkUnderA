// [rc4l] Pure inversion of the DirectInput-code to SDL-scancode keyboard table, extracted so it can be unit-tested without SDL.
#pragma once

// [rc4l] Builds the reverse lookup: scancodeToDik[scancode] = the DIK code mapping to it.
// Entry 0 of either direction means "unmapped"; when several DIK codes share a scancode the
// lowest-numbered one wins, so aliases the caller adds afterwards are not clobbered.
void ComputeInvertKeyTable(const unsigned short *dikToScancode, int dikCount,
	unsigned char *scancodeToDik, int scancodeCount);
