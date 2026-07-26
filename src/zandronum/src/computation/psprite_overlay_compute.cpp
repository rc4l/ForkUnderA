// [MGOOOOOO] Implementation of the pure A_Overlay decision/offset math. No engine deps, so
// both the engine and the standalone test build compile this TU.
#include "computation/psprite_overlay_compute.h"

bool ComputeIsReservedPSpriteLayer(int layer)
{
	return layer == ZX_PSP_WEAPON
		|| layer == ZX_PSP_FLASH
		|| layer == ZX_PSP_TARGETCENTER
		|| layer == ZX_PSP_TARGETLEFT
		|| layer == ZX_PSP_TARGETRIGHT;
}

unsigned int ComputeSortedInsertIndex(const int *layers, unsigned int count, int newLayer)
{
	if (layers == nullptr)
		return 0;

	unsigned int i = 0;
	while (i < count && layers[i] < newLayer)
		i++;

	return i;
}

bool ComputeClearOverlayShouldRemove(int layer, int start, int stop, bool safety)
{
	if (safety && ComputeIsReservedPSpriteLayer(layer))
		return false;

	if (start == 0 && stop == 0)
		return true;

	return layer >= start && layer <= stop;
}

unsigned int ComputeOverlayFlags(unsigned int existing, unsigned int flags, bool set)
{
	return set ? (existing | flags) : (existing & ~flags);
}

int64_t ComputeOverlayAxis(int64_t oldValue, int64_t newValue, bool keep, bool add)
{
	if (keep)
		return oldValue;

	if (add)
		return oldValue + newValue;

	return newValue;
}

int64_t ComputeOverlaySquareScaleY(int64_t scaleX, int64_t scaleY)
{
	return scaleY == 0 ? scaleX : scaleY;
}

void ComputeOverlayDrawOffset(bool ridesBob, int flags, int64_t weaponX, int64_t weaponY,
	int64_t bobX, int64_t bobY, int64_t *outAddX, int64_t *outAddY)
{
	int64_t ax = 0;
	int64_t ay = 0;

	if (ridesBob)
	{
		ax = bobX;
		ay = bobY;
	}
	else
	{
		if (flags & ZX_PSPF_ADDWEAPON)
		{
			ax += weaponX;
			ay += weaponY;
		}
		if (flags & ZX_PSPF_ADDBOB)
		{
			ax += bobX;
			ay += bobY;
		}
	}

	if (outAddX != nullptr)
		*outAddX = ax;
	if (outAddY != nullptr)
		*outAddY = ay;
}
