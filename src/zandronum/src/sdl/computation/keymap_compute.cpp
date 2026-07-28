// [rc4l] See keymap_compute.h.
#include "keymap_compute.h"

void ComputeInvertKeyTable(const unsigned short *dikToScancode, int dikCount,
	unsigned char *scancodeToDik, int scancodeCount)
{
	if (scancodeToDik == nullptr || scancodeCount <= 0) return;

	for (int i = 0; i < scancodeCount; ++i) scancodeToDik[i] = 0;
	if (dikToScancode == nullptr) return;

	// [rc4l] Walk downwards so the lowest DIK code wins when two map to the same scancode.
	for (int dik = dikCount - 1; dik >= 0; --dik)
	{
		const unsigned short scancode = dikToScancode[dik];
		if (scancode != 0 && scancode < (unsigned short)scancodeCount)
		{
			scancodeToDik[scancode] = (unsigned char)dik;
		}
	}
}
