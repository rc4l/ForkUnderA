// [rc4l] What to do when a DECORATE constant is defined a second time.
//
// This used to be unconditionally fatal, which meant that any global constant the engine ships is a
// landmine: a mod defining the same name aborts the whole load. A_Overlay's STYLE_* and
// A_JumpIfInput's BT_*/JIF_* did exactly that, and MM8BDM -- which brings its own STYLE_* -- hit 12
// collisions and quit before reaching the menu.
//
// The policy is now: a GLOBAL constant colliding with an existing global constant is a warning, the
// first definition (the engine's, since zandronum.pk3 is parsed before any pwad) stays in force, and
// the mod's copy is dropped. Everything else stays an error.
//
// Engine-wins is NOT always behaviour-preserving, and that is deliberate rather than overlooked. The
// STYLE_* names diverge above STYLE_Stencil: the engine's DECORATE values run 6,7,8… while the ACS
// APROP_RenderStyle values mods use run 64,65,66… (LegacyRenderStyleIndices, p_acs.cpp). A mod whose
// STYLE_Translucent means 64 now gets 6 and renders differently. So the two warning cases are kept
// distinct -- a same-value collision cannot change anything and is reported calmly, while a
// differing-value collision is called out loudly with both numbers, so a silent visual change shows
// up in the console instead of being discovered in-game.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_DUP_CONST_COMPUTE_H
#define ZX_DUP_CONST_COMPUTE_H

namespace zx
{

enum DuplicateConstantAction
{
	// [rc4l] Keep failing the load: class-scoped redefinitions are a mod contradicting itself, and a
	// name colliding with a non-constant is a genuine conflict rather than a duplicated value.
	DupConst_Error = 0,
	// [rc4l] Harmless: both definitions agree, so whichever is kept the value is identical.
	DupConst_WarnSameValue = 1,
	// [rc4l] Tolerated but consequential: the values differ, the engine's is kept, and anything in
	// the redefining file that relied on its own number will behave differently.
	DupConst_WarnValueChanged = 2,
};

// [rc4l] `isGlobalScope` is false for a constant declared inside an actor. `existingIsConstant` is
// false when the name is already taken by something that is not a constant (a native variable, a
// function). `sameValue` compares the existing value with the one being dropped.
DuplicateConstantAction ComputeDuplicateConstantAction(bool isGlobalScope, bool existingIsConstant,
                                                       bool sameValue);

} // namespace zx

#endif // ZX_DUP_CONST_COMPUTE_H
