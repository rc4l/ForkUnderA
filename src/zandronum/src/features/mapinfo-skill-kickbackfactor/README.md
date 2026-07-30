# mapinfo-skill-kickbackfactor

Skill `kickbackfactor` — scales damage kickback.
- **Provenance:** uzdoom@7267e608c · **Class:** PORTABLE
- **Hooks:** `g_level.h` (FSkillInfo::KickbackFactor, SKILLP_KickbackFactor); `g_skill.cpp` (default FRACUNIT, `kickbackfactor` parse, G_SkillProperty, operator=); `p_interaction.cpp` P_DamageMobj (FixedMul on kickback).

## Conformance
```
skill zxkb { kickbackfactor = 4.0 }
```
