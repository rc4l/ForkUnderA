// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continueshow_compute.h"

namespace zx
{

ContinueVisibility DecideContinueVisibility(const ContinueShowInputs &in)
{
	if (in.recordParsed == false)
		return ContinueVisibility::Hidden;

	switch (in.kind)
	{
	case ContinueKind::Single:
		// The snapshot has to be there, and this build has to be able to read it. Both are asked
		// here rather than at load time, because by load time the WAD set has already been swapped.
		if (in.saveFileExists == false)
			return ContinueVisibility::Hidden;
		if (in.saveVersion < in.minSaveVersion)
			return ContinueVisibility::Hidden;
		return ContinueVisibility::Shown;

	case ContinueKind::Server:
		// Only PROOF that it will not work hides it; see the header on why pending does not.
		if ((in.probe == ServerProbe::Gone) || (in.probe == ServerProbe::WadsDiffer))
			return ContinueVisibility::Hidden;
		return ContinueVisibility::Shown;

	default:
		return ContinueVisibility::Hidden;
	}
}

bool ContinueIsShown(const ContinueShowInputs &in)
{
	return (DecideContinueVisibility(in) == ContinueVisibility::Shown);
}

} // namespace zx
