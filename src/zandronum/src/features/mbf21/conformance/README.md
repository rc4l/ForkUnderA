# MBF21 conformance patch

`mbf21_conformance.deh` is a synthetic DeHackEd patch that exercises the **full**
MBF21 parse surface in one file — every one of the 28 codepointers (via
`[CODEPTR]` on DEHEXTRA frames), all 19 thing-flag mnemonics, all 6 weapon-flag
mnemonics, every MBF21 thing/weapon field (fast speed, melee range, the three
damage groups incl. a groupless `-1`, rip sound, dropped item, ammo-per-shot),
frame `Args1..8`, and the DSDHacked `[SPRITES]`/`[SOUNDS]` sections.

Run it against the engine and the DeHackEd patch must install with **no**
`Incompatible`/`Unknown`/`Bad` lines in the log:

    ZandroX -iwad freedoom2.wad -deh mbf21_conformance.deh +logfile out.log +quit
    grep -iE "Incompatible|unknown code|Bad thing|Bad sound|Bad sprite" out.log   # expect nothing

This caught the "Incompatible code pointer" bug where the jump codepointers
aliased to pre-existing functions (A_JumpIfHealthLower / A_JumpIfCloser /
A_JumpIfTargetInLOS / A_JumpIfTracerCloser) were rejected because their DECORATE
decls lacked default arguments — which also silently broke Judgment's
`JumpIfTargetCloser`.
