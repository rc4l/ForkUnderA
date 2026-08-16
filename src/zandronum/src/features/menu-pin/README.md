# menu-pin

Keeps FUA's own entries in the options menu when a mod replaces that menu.

## The problem

MENUDEF has no "insert into an existing menu" directive for mods to use. If a mod wants one line in
the options menu, the only thing it can do is copy the entire stock `OptionMenu "OptionsMenu"` into
its own MENUDEF and paste its entry into the copy.

Ghouls vs Humans: Legacy of Darkness does this in `lod-patchv1.7a.pk3/MENUDEF`. Its copy is stock
Zandronum's list with `"Ghouls vs Humans"` added at the top -- written years before this fork
existed, so naturally with no `"FUA Options"` line. Mod MENUDEFs parse after ours, the mod's
definition replaces ours wholesale, and every FUA setting vanishes from the game.

This is not the mod doing anything wrong. It is the only thing MENUDEF permits, and any mod that
touches the options menu has the same effect. `AddOptionMenu` exists in this engine and would let a
mod append instead, but a mod written before it existed cannot be expected to use it.

## The fix

`MenuPin_RestoreFuaOptions()` runs from `M_ParseMenuDefs` once every MENUDEF has been read -- the
only moment at which "did our entry survive?" is answerable. If `OptionsMenu` has no item pointing
at `FUAOptions`, one is inserted at the top, where our own `menudef.txt` puts it.

Matched on the **action**, not the label. The action is the menu being linked to, which is ours and
fixed; a label is display text a translation or re-skin could legitimately change, and matching on
`"FUA Options"` would miss a renamed-but-present entry and add a duplicate beside it.

It prints a line when it fires. A mod replacing `OptionsMenu` is probably dropping other engine
entries too, so someone staring at a menu that lost something should be told the replacement
happened rather than left to wonder.

## In-place edits outside this directory

- `menu/menudef.cpp` -- the call at the end of `M_ParseMenuDefs`, immediately before
  `GlobalHeader_ShiftMenusDown()` so the restored entry is shifted with everything else rather than
  being left behind the header bar. Declared with a local `namespace zx { ... }` forward declaration,
  matching how `GlobalHeader_ShiftMenusDown` is already declared two lines up.
